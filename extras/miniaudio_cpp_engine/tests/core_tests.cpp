#include "miniaudio_cpp_engine/audio_buffer.hpp"
#include "miniaudio_cpp_engine/device_adapter.hpp"
#include "miniaudio_cpp_engine/plan_exchange.hpp"
#include "miniaudio_cpp_engine/quantum_adapter.hpp"
#include "miniaudio_cpp_engine/render_plan.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace miniaudio::cpp_engine;

class test_failure final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

#define REQUIRE(condition)                                                                                              \
    do {                                                                                                                \
        if (!(condition))                                                                                               \
            throw test_failure{#condition};                                                                             \
    } while (false)

struct constant_source {
    float value{};
};

struct destruction_record {
    std::atomic<std::uint32_t> count{};
    std::atomic<bool> destroyed_on_expected_thread{};
    std::thread::id expected_thread{};
};

class observed_source_node final : public node {
  public:
    observed_source_node(channel_layout layout, float value, destruction_record& destruction)
        : layout_(layout), value_(value), destruction_(destruction) {}

    ~observed_source_node() override {
        destruction_.destroyed_on_expected_thread.store(
            std::this_thread::get_id() == destruction_.expected_thread, std::memory_order_relaxed);
        destruction_.count.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] auto output_layout() const noexcept -> const channel_layout& override { return layout_; }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return 0; }
    [[nodiscard]] auto accepts_input(std::uint32_t, const channel_layout&) const noexcept -> bool override {
        return false;
    }
    void process(process_context, std::span<const const_planar_view>, planar_view output) noexcept override {
        for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
            std::fill(output.channel(channel).begin(), output.channel(channel).end(), value_);
    }

  private:
    channel_layout layout_;
    float value_{};
    destruction_record& destruction_;
};

void fill_constant(void* opaque, process_context, planar_view output) noexcept {
    const auto value = static_cast<const constant_source*>(opaque)->value;
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        std::fill(output.channel(channel).begin(), output.channel(channel).end(), value);
}

void fill_timeline(void*, process_context context, planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (std::uint32_t frame = 0; frame < output.frames(); ++frame) {
            output.channel_data(channel)[frame] =
                static_cast<float>(context.first_frame + frame + static_cast<std::uint64_t>(channel) * 10'000U);
        }
    }
}

void negate_effect(void*, process_context, const_planar_view input, planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
            output.channel_data(channel)[frame] = -input.channel_data(channel)[frame];
    }
}

auto make_observed_plan(stream_contract contract, float value, destruction_record& destruction)
    -> std::unique_ptr<render_plan> {
    render_plan_builder builder{contract};
    const auto output = builder.add(
        std::make_unique<observed_source_node>(contract.layout, value, destruction));
    return std::move(builder).compile(output);
}

class shifting_reference_adapter {
  public:
    explicit shifting_reference_adapter(stream_contract contract)
        : contract_(contract), cache_(contract.layout, contract.quantum) {}

    template <class Render> void pull(interleaved_view output, Render&& render) noexcept {
        std::uint32_t destination_offset = 0;
        while (destination_offset < output.frames()) {
            if (available_ == 0) {
                render(process_context{next_frame_, contract_.quantum, contract_.sample_rate}, cache_.view());
                next_frame_ += contract_.quantum;
                available_ = contract_.quantum;
            }

            const auto count = std::min(output.frames() - destination_offset, available_);
            const auto cache = cache_.view();
            interleave_frames(cache, 0, output, destination_offset, count);

            available_ -= count;
            if (available_ != 0) {
                for (std::uint32_t channel = 0; channel < cache.channels(); ++channel) {
                    std::memmove(cache.channel_data(channel), cache.channel_data(channel) + count,
                                 static_cast<std::size_t>(available_) * sizeof(float));
                }
            }
            destination_offset += count;
        }
    }

  private:
    stream_contract contract_;
    planar_buffer cache_;
    std::uint32_t available_{};
    std::uint64_t next_frame_{};
};

void buffers_preserve_layout_and_round_trip() {
    const auto layout = channel_layout::stereo();
    planar_buffer planar{layout, 3};
    REQUIRE(reinterpret_cast<std::uintptr_t>(planar.data()) % audio_buffer_alignment == 0);

    std::array<float, 6> interleaved{1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f};
    deinterleave(const_interleaved_view{interleaved.data(), layout, 3}, planar.view());
    REQUIRE(planar.view().channel(0)[2] == 3.0f);
    REQUIRE(planar.view().channel(1)[1] == 20.0f);

    std::array<float, 6> result{};
    interleave(planar.view(), interleaved_view{result.data(), layout, 3});
    REQUIRE(result == interleaved);
    REQUIRE(channel_layout::mono() != channel_layout{channel_role::front_center});

    auto rejected_duplicate = false;
    try {
        static_cast<void>(channel_layout{channel_role::front_left, channel_role::front_left});
    } catch (const std::invalid_argument&) {
        rejected_duplicate = true;
    }
    REQUIRE(rejected_duplicate);

    auto rejected_mixed_mono = false;
    try {
        static_cast<void>(channel_layout{channel_role::mono, channel_role::unknown});
    } catch (const std::invalid_argument&) {
        rejected_mixed_mono = true;
    }
    REQUIRE(rejected_mixed_mono);
    REQUIRE((channel_layout{channel_role::unknown, channel_role::unknown}.size() == 2));
}

void graph_composes_source_gain_mixer_and_effect() {
    const stream_contract contract{channel_layout::stereo(), 48'000, 8};
    constant_source first{1.0f};
    constant_source second{0.5f};

    render_plan_builder builder{contract};
    const auto first_source = builder.add(
        std::make_unique<callback_source_node>(contract.layout, &first, fill_constant));
    const std::array first_input{first_source};
    const auto gained = builder.add(std::make_unique<gain_node>(contract.layout, 2.0f), first_input);
    const auto second_source = builder.add(
        std::make_unique<callback_source_node>(contract.layout, &second, fill_constant));
    const std::array mixed_inputs{gained, second_source};
    const auto mixed = builder.add(std::make_unique<mixer_node>(contract.layout, 2), mixed_inputs);
    const std::array effect_input{mixed};
    const auto effected = builder.add(
        std::make_unique<callback_effect_node>(contract.layout, contract.layout, nullptr, negate_effect), effect_input);
    auto plan = std::move(builder).compile(effected);

    planar_buffer output{contract.layout, contract.quantum};
    plan->process({0, contract.quantum, contract.sample_rate}, output.view());
    const auto output_view = output.view();
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (const auto sample : output_view.channel(channel))
            REQUIRE(sample == -2.5f);
    }

    plan->process({contract.quantum, contract.quantum, contract.sample_rate}, output.view());
    const auto second_output_view = output.view();
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (const auto sample : second_output_view.channel(channel))
            REQUIRE(sample == -2.5f);
    }
}

void gain_matches_the_miniaudio_reference() {
    const auto layout = channel_layout::stereo();
    constexpr std::uint32_t frames = 8;
    std::array<float, frames * 2> input{
        0.0f, -0.0f, 1.0f, -1.0f, 0.25f, -0.25f, 10.0f, -10.0f,
        1.0e-20f, -1.0e-20f, 3.5f, -7.25f, 65'504.0f, -65'504.0f, 0.75f, -0.75f};
    std::array<float, frames * 2> reference{};

    ma_gainer gainer{};
    auto config = ma_gainer_config_init(2, 0);
    REQUIRE(ma_gainer_init(&config, nullptr, &gainer) == MA_SUCCESS);
    REQUIRE(ma_gainer_set_gain(&gainer, 0.5f) == MA_SUCCESS);
    REQUIRE(ma_gainer_process_pcm_frames(&gainer, reference.data(), input.data(), frames) == MA_SUCCESS);
    ma_gainer_uninit(&gainer, nullptr);

    planar_buffer planar_input{layout, frames};
    planar_buffer planar_output{layout, frames};
    deinterleave(const_interleaved_view{input.data(), layout, frames}, planar_input.view());
    gain_node candidate{layout, 0.5f};
    const std::array<const_planar_view, 1> inputs{planar_input.view()};
    candidate.process({0, frames, 48'000}, inputs, planar_output.view());

    std::array<float, frames * 2> actual{};
    interleave(planar_output.view(), interleaved_view{actual.data(), layout, frames});
    for (std::size_t index = 0; index < actual.size(); ++index)
        REQUIRE(std::bit_cast<std::uint32_t>(actual[index]) == std::bit_cast<std::uint32_t>(reference[index]));
}

void callback_sizes_preserve_the_frame_timeline() {
    const stream_contract contract{channel_layout::stereo(), 48'000, 8};
    render_plan_builder builder{contract};
    const auto source = builder.add(
        std::make_unique<callback_source_node>(contract.layout, nullptr, fill_timeline));
    auto plan = std::move(builder).compile(source);
    quantum_output_adapter adapter{contract};
    shifting_reference_adapter reference{contract};

    std::uint64_t expected_frame = 0;
    for (const auto request : std::array<std::uint32_t, 7>{3, 5, 1, 13, 8, 2, 11}) {
        std::vector<float> output(static_cast<std::size_t>(request) * contract.layout.size());
        std::vector<float> reference_output(output.size());
        adapter.pull(interleaved_view{output.data(), contract.layout, request},
                     [&](process_context context, planar_view block) noexcept { plan->process(context, block); });
        reference.pull(interleaved_view{reference_output.data(), contract.layout, request},
                       [&](process_context context, planar_view block) noexcept { plan->process(context, block); });
        REQUIRE(output == reference_output);
        for (std::uint32_t frame = 0; frame < request; ++frame) {
            REQUIRE(output[static_cast<std::size_t>(frame) * 2] == static_cast<float>(expected_frame + frame));
            REQUIRE(output[static_cast<std::size_t>(frame) * 2 + 1] ==
                    static_cast<float>(10'000 + expected_frame + frame));
        }
        expected_frame += request;
    }
    REQUIRE(adapter.counters().delivered_frames == expected_frame);
    REQUIRE(adapter.counters().rendered_frames % contract.quantum == 0);
}

void plan_reclamation_waits_for_a_new_quantum() {
    const stream_contract contract{channel_layout::mono(), 48'000, 8};
    const auto control_thread = std::this_thread::get_id();
    destruction_record first_destruction;
    destruction_record second_destruction;
    first_destruction.expected_thread = control_thread;
    second_destruction.expected_thread = control_thread;
    plan_exchange exchange{contract};
    auto first_plan = make_observed_plan(contract, 1.0f, first_destruction);
    REQUIRE(exchange.try_publish(first_plan) == publish_status::published);
    REQUIRE(!first_plan);

    render_plan* active = nullptr;
    quantum_output_adapter adapter{contract};
    auto render = [&](process_context context, planar_view output) noexcept {
        exchange.audio_acquire(active);
        if (active)
            active->process(context, output);
        else
            clear(output);
    };

    std::array<float, 3> first_part{};
    adapter.pull(interleaved_view{first_part.data(), contract.layout, 3}, render);
    REQUIRE(exchange.acknowledged_generation() == 1);
    REQUIRE((first_part == std::array<float, 3>{1.0f, 1.0f, 1.0f}));

    auto second_plan = make_observed_plan(contract, 2.0f, second_destruction);
    REQUIRE(exchange.try_publish(second_plan) == publish_status::published);
    std::array<float, 5> old_cache{};
    adapter.pull(interleaved_view{old_cache.data(), contract.layout, 5}, render);
    REQUIRE(exchange.acknowledged_generation() == 1);
    REQUIRE(exchange.reclaim_acknowledged() == 0);
    REQUIRE(first_destruction.count.load(std::memory_order_acquire) == 0);
    REQUIRE((old_cache == std::array<float, 5>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}));

    std::array<float, 1> new_quantum{};
    adapter.pull(interleaved_view{new_quantum.data(), contract.layout, 1}, render);
    REQUIRE(new_quantum[0] == 2.0f);
    REQUIRE(exchange.acknowledged_generation() == 2);
    REQUIRE(first_destruction.count.load(std::memory_order_acquire) == 0);
    REQUIRE(exchange.reclaim_acknowledged() == 1);
    REQUIRE(first_destruction.count.load(std::memory_order_acquire) == 1);
    REQUIRE(first_destruction.destroyed_on_expected_thread.load(std::memory_order_relaxed));
    REQUIRE(second_destruction.count.load(std::memory_order_acquire) == 0);
    exchange.reset_after_stop();
    REQUIRE(first_destruction.count.load(std::memory_order_acquire) == 1);
    REQUIRE(second_destruction.count.load(std::memory_order_acquire) == 1);
    REQUIRE(second_destruction.destroyed_on_expected_thread.load(std::memory_order_relaxed));
}

void plan_exchange_is_safe_across_control_and_render_threads() {
    constexpr std::size_t plan_count = 256;
    const stream_contract contract{channel_layout::mono(), 48'000, 8};
    const auto control_thread = std::this_thread::get_id();
    std::array<destruction_record, plan_count> destructions{};
    for (auto& destruction : destructions)
        destruction.expected_thread = control_thread;

    plan_exchange exchange{contract};
    std::atomic<bool> start{};
    std::atomic<bool> stop{};
    std::atomic<bool> failed{};
    std::thread render_thread{[&] {
        render_plan* active = nullptr;
        planar_buffer output{contract.layout, contract.quantum};
        start.store(true, std::memory_order_release);
        std::size_t adopted = 0;
        while (adopted < plan_count && !stop.load(std::memory_order_acquire)) {
            if (!exchange.audio_acquire(active)) {
                std::this_thread::yield();
                continue;
            }
            active->process({adopted * contract.quantum, contract.quantum, contract.sample_rate}, output.view());
            ++adopted;
            if (output.view().channel(0)[0] != static_cast<float>(adopted))
                failed.store(true, std::memory_order_release);
        }
    }};

    while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
    for (std::size_t index = 0; index < plan_count; ++index) {
        auto plan = make_observed_plan(contract, static_cast<float>(index + 1), destructions[index]);
        for (;;) {
            const auto status = exchange.try_publish(plan);
            if (status == publish_status::published)
                break;
            if (status != publish_status::busy) {
                failed.store(true, std::memory_order_release);
                stop.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::yield();
        }
        if (stop.load(std::memory_order_acquire))
            break;
        if (plan) {
            failed.store(true, std::memory_order_release);
            stop.store(true, std::memory_order_release);
            break;
        }
        while (exchange.acknowledged_generation() < index + 1)
            std::this_thread::yield();
        const auto reclaimed = exchange.reclaim_acknowledged();
        const auto expected_reclaimed = index == 0 ? 0U : 1U;
        if (reclaimed != expected_reclaimed ||
            destructions[index].count.load(std::memory_order_acquire) != 0 ||
            (index != 0 &&
             (destructions[index - 1].count.load(std::memory_order_acquire) != 1 ||
              !destructions[index - 1].destroyed_on_expected_thread.load(std::memory_order_relaxed)))) {
            failed.store(true, std::memory_order_release);
            stop.store(true, std::memory_order_release);
            break;
        }
    }

    render_thread.join();
    REQUIRE(!failed.load(std::memory_order_acquire));
    REQUIRE(exchange.acknowledged_generation() == plan_count);
    REQUIRE(destructions.back().count.load(std::memory_order_acquire) == 0);
    exchange.reset_after_stop();
    for (const auto& destruction : destructions) {
        REQUIRE(destruction.count.load(std::memory_order_acquire) == 1);
        REQUIRE(destruction.destroyed_on_expected_thread.load(std::memory_order_relaxed));
    }
}

void device_adapter_preserves_or_rejects_channel_semantics() {
    const stream_contract mixed_contract{
        channel_layout{channel_role::front_right, channel_role::unknown}, 48'000, 128};
    plan_exchange mixed_exchange{mixed_contract};
    playback_device mixed_device{
        mixed_exchange, playback_device_config{mixed_contract, ma_backend_null, mixed_contract.quantum}};
    REQUIRE(mixed_device.initialize() == MA_INVALID_ARGS);
    REQUIRE(!mixed_device.initialized());

#if defined(MINIAUDIO_CPP_ENGINE_TEST_NULL_BACKEND)
    const stream_contract contract{channel_layout::stereo(), 48'000, 128};
    plan_exchange exchange{contract};
    playback_device device{exchange, playback_device_config{contract, ma_backend_null, contract.quantum}};
    REQUIRE(device.initialize() == MA_SUCCESS);
    REQUIRE(device.initialized());
    device.uninitialize();
    REQUIRE(!device.initialized());
#endif
}

using test_function = void (*)();

int run(std::string_view name, test_function test) {
    try {
        test();
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}
} // namespace

int main() {
    int failures = 0;
    failures += run("buffers_preserve_layout_and_round_trip", buffers_preserve_layout_and_round_trip);
    failures += run("graph_composes_source_gain_mixer_and_effect", graph_composes_source_gain_mixer_and_effect);
    failures += run("gain_matches_the_miniaudio_reference", gain_matches_the_miniaudio_reference);
    failures += run("callback_sizes_preserve_the_frame_timeline", callback_sizes_preserve_the_frame_timeline);
    failures += run("plan_reclamation_waits_for_a_new_quantum", plan_reclamation_waits_for_a_new_quantum);
    failures += run("plan_exchange_is_safe_across_control_and_render_threads",
                    plan_exchange_is_safe_across_control_and_render_threads);
    failures += run("device_adapter_preserves_or_rejects_channel_semantics",
                    device_adapter_preserves_or_rejects_channel_semantics);
    return failures == 0 ? 0 : 1;
}
