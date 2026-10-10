#include "miniaudio_cpp_engine/device_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace miniaudio::cpp_engine {
// Exercise the real callback renderer deterministically, without a physical
// device, scheduler-dependent waits, or widening the public playback API.
struct playback_device_test_access {
    static void render(playback_device& device, std::span<float> output) {
        device.render(output.data(), static_cast<ma_uint32>(output.size()));
    }
};
} // namespace miniaudio::cpp_engine

namespace {
using namespace miniaudio::cpp_engine;
const stream_contract contract{channel_layout::mono(), 48'000, 8};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct clip_state {
    std::span<const float> samples;
    std::size_t cursor{};
    std::uint32_t calls{};
};

auto read_clip(void* opaque, process_context, planar_view output) noexcept -> source_result {
    auto& clip = *static_cast<clip_state*>(opaque);
    ++clip.calls;
    const auto count = std::min<std::size_t>(output.frames(), clip.samples.size() - clip.cursor);
    if (count != 0)
        std::copy_n(clip.samples.data() + clip.cursor, count, output.channel_data(0));
    clip.cursor += count;
    return {static_cast<std::uint32_t>(count), clip.cursor == clip.samples.size()};
}

struct constant_state { float value{}; std::uint32_t calls{}; };
void fill_constant(void* opaque, process_context, planar_view output) noexcept {
    auto& source = *static_cast<constant_state*>(opaque);
    ++source.calls;
    std::fill(output.channel(0).begin(), output.channel(0).end(), source.value);
}

void negate(void*, process_context, const_planar_view input, planar_view output) noexcept {
    for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
        output.channel_data(0)[frame] = -input.channel_data(0)[frame];
}

struct destruction_record { std::uint32_t count{}; bool owner_thread{}; std::thread::id owner; };

class fir_node final : public stream_node {
  public:
    explicit fir_node(destruction_record* destruction = nullptr) : destruction_(destruction) {}
    ~fir_node() override {
        if (destruction_) {
            ++destruction_->count;
            destruction_->owner_thread = std::this_thread::get_id() == destruction_->owner;
        }
    }
    auto output_layout() const noexcept -> const channel_layout& override { return layout_; }
    auto input_count() const noexcept -> std::uint32_t override { return 1; }
    auto accepts_input(std::uint32_t index, const channel_layout& layout) const noexcept -> bool override {
        return index == 0 && layout == layout_;
    }
    auto process_stream(process_context, std::span<const stream_input> inputs, planar_view output) noexcept
        -> stream_result override {
        clear(output);
        const auto input = inputs.front();
        const auto count = input.result.valid_frames;
        had_input_ = had_input_ || count != 0;
        if (!ended_ && input.result.state == stream_state::complete) {
            ended_ = true;
            tail_ = had_input_ ? 2U : 0U;
        }
        const auto frames = ended_ ? std::min(output.frames(), count + tail_) : output.frames();
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const float sample = frame < count ? input.samples.channel_data(0)[frame] : 0.0f;
            output.channel_data(0)[frame] = sample + 0.5f * previous_ + 0.25f * earlier_;
            earlier_ = previous_;
            previous_ = sample;
        }
        if (ended_)
            tail_ -= frames - count;
        return {frames, ended_ && tail_ == 0 ? stream_state::complete : stream_state::running};
    }
  private:
    channel_layout layout_{channel_layout::mono()};
    float previous_{};
    float earlier_{};
    std::uint32_t tail_{};
    bool ended_{};
    bool had_input_{};
    destruction_record* destruction_{};
};

auto make_clip_plan(clip_state& clip, std::uint32_t effects = 0, destruction_record* destruction = nullptr)
    -> std::unique_ptr<render_plan> {
    render_plan_builder builder{contract};
    auto output = builder.add(std::make_unique<finite_source_node>(contract.layout, &clip, read_clip));
    for (std::uint32_t index = 0; index < effects; ++index) {
        const std::array input{output};
        output = builder.add(std::make_unique<fir_node>(destruction), input);
    }
    return std::move(builder).compile_stream(output);
}

auto collect(render_plan& plan) -> std::vector<float> {
    quantum_output_adapter adapter{contract};
    std::vector<float> result;
    constexpr std::array<std::uint32_t, 7> requests{3, 5, 1, 13, 8, 2, 11};
    bool complete = false;
    for (std::size_t step = 0; step < 64 && !complete; ++step) {
        std::array<float, 13> output{};
        const auto frames = requests[step % requests.size()];
        const auto status = adapter.pull_stream(interleaved_view{output.data(), contract.layout, frames},
            [&](process_context context, planar_view block) noexcept { return plan.process_stream(context, block); });
        require(status.state != stream_state::failed, "finite collection failed");
        result.insert(result.end(), output.begin(), output.begin() + status.valid_frames);
        for (auto index = status.valid_frames; index < frames; ++index)
            require(output[index] == 0.0f, "terminal callback suffix was not silent");
        complete = status.state == stream_state::complete;
    }
    require(complete, "finite collection did not terminate within its proved bound");
    const auto last = adapter.pull_stream(interleaved_view{nullptr, contract.layout, 0},
        [&](process_context, planar_view) noexcept -> stream_result {
            return {0, stream_state::failed};
        });
    require(last.state == stream_state::complete && last.valid_frames == 0, "completed empty pull changed state");
    return result;
}

auto convolution(std::span<const float> input, std::span<const float> kernel) -> std::vector<float> {
    if (input.empty())
        return {};
    std::vector<float> output(input.size() + kernel.size() - 1);
    for (std::size_t sample = 0; sample < input.size(); ++sample)
        for (std::size_t tap = 0; tap < kernel.size(); ++tap)
            output[sample + tap] += input[sample] * kernel[tap];
    return output;
}

void finite_sources_and_serial_tails() {
    constexpr std::array<float, 3> kernel{1.0f, 0.5f, 0.25f};
    for (const auto length : std::array<std::size_t, 7>{0, 1, 7, 8, 9, 16, 19}) {
        std::vector<float> samples(length);
        for (std::size_t index = 0; index < length; ++index)
            samples[index] = static_cast<float>(index + 1);
        for (const auto effects : std::array<std::uint32_t, 3>{0, 1, 2}) {
            clip_state clip{samples};
            auto plan = make_clip_plan(clip, effects);
            auto expected = samples;
            for (std::uint32_t index = 0; index < effects; ++index)
                expected = convolution(expected, kernel);
            require(collect(*plan) == expected, "finite source/FIR timeline differs from mathematical convolution");
            const auto expected_calls = length == 0 ? 1U : static_cast<std::uint32_t>((length + 7) / 8);
            require(clip.calls == expected_calls, "source was invoked after EOF or before all data was read");
            require(clip.cursor == length, "source cursor did not reach exact EOF");
        }
    }
}

void mixer_and_tail_free_effect() {
    const std::array<float, 3> first{1, 2, 3};
    const std::array<float, 11> second{10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    clip_state a{first}, b{second};
    render_plan_builder builder{contract};
    const auto left = builder.add(std::make_unique<finite_source_node>(contract.layout, &a, read_clip));
    const auto right = builder.add(std::make_unique<finite_source_node>(contract.layout, &b, read_clip));
    const std::array inputs{left, right};
    const auto mixer = builder.add(std::make_unique<mixer_node>(contract.layout, 2), inputs);
    const std::array effect_input{mixer};
    const auto output = builder.add(std::make_unique<tail_free_effect_node>(contract.layout, contract.layout, nullptr, negate),
                                    effect_input);
    auto plan = std::move(builder).compile_stream(output);
    const std::vector<float> expected{-11, -13, -15, -13, -14, -15, -16, -17, -18, -19, -20};
    require(collect(*plan) == expected, "mixer completed early, repeated ended data, or lost its final partial block");
    require(a.calls == 1 && b.calls == 2, "mixer continued reading an ended source");
}

auto starved_read(void* opaque, process_context, planar_view output) noexcept -> source_result {
    auto& calls = *static_cast<std::uint32_t*>(opaque);
    if (calls++ == 0)
        return {0, false};
    output.channel_data(0)[0] = 9;
    output.channel_data(0)[1] = 10;
    return {2, true};
}

auto invalid_read(void*, process_context, planar_view output) noexcept -> source_result {
    return {output.frames() + 1, true};
}

void starvation_failure_and_compilation() {
    std::uint32_t calls = 0;
    render_plan_builder builder{contract};
    const auto source = builder.add(std::make_unique<finite_source_node>(contract.layout, &calls, starved_read));
    const std::array input{source};
    // An unreachable opaque callback must neither reject nor execute in stream mode.
    static_cast<void>(builder.add(std::make_unique<callback_effect_node>(contract.layout, contract.layout, nullptr, negate), input));
    auto plan = std::move(builder).compile_stream(source);
    const std::vector<float> expected{0, 0, 0, 0, 0, 0, 0, 0, 9, 10};
    require(collect(*plan) == expected && calls == 2, "temporary starvation was treated as EOF");

    render_plan_builder opaque{contract};
    const auto s = opaque.add(std::make_unique<finite_source_node>(contract.layout, &calls, starved_read));
    const std::array opaque_input{s};
    const auto effect = opaque.add(std::make_unique<callback_effect_node>(contract.layout, contract.layout, nullptr, negate), opaque_input);
    bool rejected = false;
    try { static_cast<void>(std::move(opaque).compile_stream(effect)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "unknown legacy effect tail was silently assumed empty");

    render_plan_builder invalid{contract};
    const auto bad = invalid.add(std::make_unique<finite_source_node>(contract.layout, nullptr, invalid_read));
    auto bad_plan = std::move(invalid).compile_stream(bad);
    quantum_output_adapter adapter{contract};
    std::array<float, 11> output{};
    const auto result = adapter.pull_stream(interleaved_view{output.data(), contract.layout, 11},
        [&](process_context context, planar_view block) noexcept { return bad_plan->process_stream(context, block); });
    require(result.valid_frames == 0 && result.state == stream_state::failed, "invalid source result did not fail closed");
    require(std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0; }), "failure output was not silent");

    quantum_output_adapter malformed{contract};
    const auto invalid_result = malformed.pull_stream(interleaved_view{output.data(), contract.layout, 11},
        [](process_context, planar_view) noexcept -> stream_result { return {7, stream_state::running}; });
    require(invalid_result.state == stream_state::failed, "short running renderer result was accepted");
}

void partial_cache_and_planar_delivery() {
    const std::array<float, 5> samples{1, 2, 3, 4, 5};
    clip_state clip{samples};
    auto plan = make_clip_plan(clip);
    quantum_output_adapter adapter{contract};
    planar_buffer output{contract.layout, 2};
    auto render = [&](process_context context, planar_view block) noexcept { return plan->process_stream(context, block); };
    auto result = adapter.pull_stream(output.view(), render);
    require(result.state == stream_state::running && result.valid_frames == 2, "DSP EOF was reported before cache delivery");
    require(output.view().channel_data(0)[0] == 1 && output.view().channel_data(0)[1] == 2 && adapter.cached_frames() == 3,
            "partial final cache cursor is wrong");
    result = adapter.pull_stream(output.view(), render);
    require(result.state == stream_state::running && output.view().channel_data(0)[0] == 3 && output.view().channel_data(0)[1] == 4,
            "second partial final cache delivery is wrong");
    result = adapter.pull_stream(output.view(), render);
    require(result.state == stream_state::complete && result.valid_frames == 1 && output.view().channel_data(0)[0] == 5 &&
            output.view().channel_data(0)[1] == 0 && adapter.stream_finished(), "final cached sample was dropped or completion was early");
    require(clip.calls == 1 && adapter.counters().rendered_frames == 8 && adapter.counters().delivered_frames == 6,
            "finite cache invoked DSP again or changed counter semantics");
}

auto make_constant_plan(constant_state& source, bool stream, bool fir = false) -> std::unique_ptr<render_plan> {
    render_plan_builder builder{contract};
    auto output = builder.add(std::make_unique<callback_source_node>(contract.layout, &source, fill_constant));
    if (fir) {
        const std::array input{output};
        output = builder.add(std::make_unique<fir_node>(), input);
    }
    return stream ? std::move(builder).compile_stream(output) : std::move(builder).compile(output);
}

void device_graceful_stop_and_stale_requests() {
    plan_exchange exchange{contract};
    playback_device device{exchange, playback_device_config{contract, {}, 0}};
    require(device.request_drain() == drain_request_status::no_active_plan, "empty drain request was accepted");
    constant_state first{1}, second{2};
    auto plan = make_constant_plan(first, true, true);
    require(exchange.try_publish(plan) == publish_status::published, "first publication failed");
    std::array<float, 8> output{};
    playback_device_test_access::render(device, output);
    require(first.calls == 1 && device.completed_generation() == 0, "continuous source completed spontaneously");
    require(device.request_drain() == drain_request_status::queued && device.request_drain() == drain_request_status::queued,
            "graceful drain was not idempotently queued");
    playback_device_test_access::render(device, output);
    require(output[0] == 0.75f && output[1] == 0.25f && output[2] == 0 && first.calls == 1 && device.completed_generation() == 1,
            "graceful drain consumed another source block or lost its FIR tail");

    auto replacement = make_constant_plan(second, true, true);
    require(exchange.try_publish(replacement) == publish_status::published, "replacement publication failed");
    playback_device_test_access::render(device, output);
    require(second.calls == 1 && output[0] == 2 && output[7] == 3.5f && device.completed_generation() == 1,
            "stale drain request affected replacement or completed adapter prevented adoption");
    require(device.request_drain() == drain_request_status::queued, "replacement drain request failed");
    constant_state third{3};
    auto interrupted = make_constant_plan(third, true);
    require(exchange.try_publish(interrupted) == publish_status::published, "interrupting publication failed");
    playback_device_test_access::render(device, output);
    require(third.calls == 1 && output[0] == 3 && device.completed_generation() == 1 && exchange.acknowledged_generation() == 3,
            "request queued for the superseded active generation drained a pending generation");
    exchange.reclaim_acknowledged();
}

void device_delivery_reclamation_and_failure() {
    plan_exchange exchange{contract};
    playback_device device{exchange, playback_device_config{contract, {}, 0}};
    const std::array<float, 1> samples{1};
    clip_state clip{samples};
    destruction_record destruction{0, false, std::this_thread::get_id()};
    auto plan = make_clip_plan(clip, 1, &destruction);
    require(exchange.try_publish(plan) == publish_status::published, "observed publication failed");
    std::array<float, 1> one{};
    std::thread render_thread{[&] { playback_device_test_access::render(device, one); }};
    render_thread.join();
    require(one[0] == 1 && device.completed_generation() == 0 && destruction.count == 0, "completion/destruction preceded cached tail");
    std::array<float, 4> rest{};
    playback_device_test_access::render(device, rest);
    require(rest[0] == 0.5f && rest[1] == 0.25f && rest[2] == 0 && device.completed_generation() == 1,
            "last tail samples were not delivered before completion");
    require(exchange.reclaim_acknowledged() == 0 && destruction.count == 0, "active complete plan was reclaimed without quiescence");

    render_plan_builder invalid{contract};
    const auto source = invalid.add(std::make_unique<finite_source_node>(contract.layout, nullptr, invalid_read));
    auto bad = std::move(invalid).compile_stream(source);
    require(exchange.try_publish(bad) == publish_status::published, "failed-stream publication failed");
    playback_device_test_access::render(device, rest);
    require(device.failed_generation() == 2 && device.completed_generation() == 1, "failed stream falsely reported success");
    require(exchange.reclaim_acknowledged() == 1 && destruction.count == 1 && destruction.owner_thread,
            "retirement did not occur on the control owner");

    constant_state legacy{4};
    auto old = make_constant_plan(legacy, false);
    require(exchange.try_publish(old) == publish_status::published, "legacy replacement publication failed");
    playback_device_test_access::render(device, rest);
    require(rest[0] == 4 && device.request_drain() == drain_request_status::unsupported_plan && device.completed_generation() == 1,
            "legacy rendering changed or unsupported drain was accepted");
}
} // namespace

int main() {
    try {
        finite_sources_and_serial_tails();
        mixer_and_tail_free_effect();
        starvation_failure_and_compilation();
        partial_cache_and_planar_delivery();
        device_graceful_stop_and_stale_requests();
        device_delivery_reclamation_and_failure();
        std::cout << "PASS finite_graph_cache_and_generation_scoped_drain\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
