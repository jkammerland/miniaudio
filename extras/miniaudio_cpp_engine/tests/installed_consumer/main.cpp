#include <miniaudio_cpp_engine/device_adapter.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <utility>

using namespace miniaudio::cpp_engine;

namespace {
struct mono_clip {
    std::span<const float> samples;
    std::size_t cursor{};
    std::uint32_t calls{};
};

auto read_clip(void* state, process_context, planar_view output) noexcept -> source_result {
    auto& clip = *static_cast<mono_clip*>(state);
    ++clip.calls;
    const auto count = static_cast<std::uint32_t>(
        std::min<std::size_t>(output.frames(), clip.samples.size() - clip.cursor));
    if (count != 0)
        std::copy_n(clip.samples.data() + clip.cursor, count, output.channel_data(0));
    clip.cursor += count;
    return {count, clip.cursor == clip.samples.size()};
}

void continuous(void* state, process_context, planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        std::fill(output.channel(channel).begin(), output.channel(channel).end(), *static_cast<float*>(state));
}

auto run() -> bool {
    const stream_contract contract{channel_layout::mono(), 48'000, 8};
    float constant = 1.25f;
    render_plan_builder old_builder{contract};
    const auto old_source = old_builder.add(
        std::make_unique<callback_source_node>(contract.layout, &constant, continuous));
    auto old_plan = std::move(old_builder).compile(old_source);
    planar_buffer old_output{contract.layout, contract.quantum};
    old_plan->process({0, contract.quantum, contract.sample_rate}, old_output.view());
    for (float sample : old_output.view().channel(0))
        if (sample != constant)
            return false;

    const std::array pcm{1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    mono_clip clip{pcm};
    render_plan_builder builder{contract};
    const auto source = builder.add(std::make_unique<finite_source_node>(contract.layout, &clip, read_clip));
    auto plan = std::move(builder).compile_stream(source);
    quantum_output_adapter adapter{contract};
    auto render = [&](process_context context, planar_view output) noexcept {
        return plan->process_stream(context, output);
    };
    std::array<float, 2> first{};
    const auto before = adapter.pull_stream(interleaved_view{first.data(), contract.layout, 2}, render);
    if (before.state != stream_state::running || before.valid_frames != 2 || first != std::array{1.0f, 2.0f})
        return false;
    std::array<float, 4> final{-1.0f, -1.0f, -1.0f, -1.0f};
    const auto end = adapter.pull_stream(interleaved_view{final.data(), contract.layout, 4}, render);
    if (end.state != stream_state::complete || end.valid_frames != 3 ||
        final != std::array{3.0f, 4.0f, 5.0f, 0.0f} || clip.calls != 1)
        return false;

    plan_exchange exchange{contract};
    playback_device device{exchange, {contract, ma_backend_null, 0}};
    if (device.request_drain() != drain_request_status::no_active_plan ||
        device.completed_generation() != 0 || device.failed_generation() != 0)
        return false;
    render_plan_builder draining{contract};
    const auto running = draining.add(std::make_unique<callback_source_node>(contract.layout, &constant, continuous));
    auto continuous_plan = std::move(draining).compile_stream(running);
    if (exchange.try_publish(continuous_plan) != publish_status::published)
        return false;
    render_plan* active = nullptr;
    if (!exchange.audio_acquire(active) || device.request_drain() != drain_request_status::queued ||
        !exchange.audio_drain_requested())
        return false;
    active->begin_drain();
    const auto drained = active->process_stream({0, contract.quantum, contract.sample_rate}, old_output.view());
    if (drained.state != stream_state::complete || drained.valid_frames != 0)
        return false;
    for (float sample : old_output.view().channel(0))
        if (sample != 0.0f)
            return false;
    return true;
}
} // namespace

int main() {
    if (!run()) {
        std::cerr << "installed legacy/stream consumer failed\n";
        return 1;
    }
    std::cout << "PASS installed legacy/stream consumer (no physical device)\n";
}
