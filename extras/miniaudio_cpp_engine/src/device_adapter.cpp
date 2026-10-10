#include "miniaudio_cpp_engine/device_adapter.hpp"

#include <array>

namespace miniaudio::cpp_engine {

namespace {
auto to_miniaudio_channel(channel_role role) noexcept -> ma_channel {
    switch (role) {
        case channel_role::mono: return MA_CHANNEL_MONO;
        case channel_role::front_left: return MA_CHANNEL_FRONT_LEFT;
        case channel_role::front_right: return MA_CHANNEL_FRONT_RIGHT;
        case channel_role::front_center: return MA_CHANNEL_FRONT_CENTER;
        case channel_role::low_frequency: return MA_CHANNEL_LFE;
        case channel_role::back_left: return MA_CHANNEL_BACK_LEFT;
        case channel_role::back_right: return MA_CHANNEL_BACK_RIGHT;
        case channel_role::side_left: return MA_CHANNEL_SIDE_LEFT;
        case channel_role::side_right: return MA_CHANNEL_SIDE_RIGHT;
        case channel_role::unknown: return MA_CHANNEL_NONE;
    }
    return MA_CHANNEL_NONE;
}
} // namespace

playback_device::playback_device(plan_exchange& exchange, playback_device_config config)
    : exchange_(exchange), config_(config), renderer_(config.contract) {}

playback_device::~playback_device() { uninitialize(); }

auto playback_device::initialize() noexcept -> ma_result {
    if (device_initialized_)
        return MA_INVALID_OPERATION;
    if (config_.contract != exchange_.contract())
        return MA_INVALID_ARGS;

    std::array<ma_channel, max_channels> channel_map{};
    auto has_unknown_channel = false;
    auto has_named_channel = false;
    for (std::uint32_t channel = 0; channel < config_.contract.layout.size(); ++channel) {
        channel_map[channel] = to_miniaudio_channel(config_.contract.layout[channel]);
        has_unknown_channel = has_unknown_channel || channel_map[channel] == MA_CHANNEL_NONE;
        has_named_channel = has_named_channel || channel_map[channel] != MA_CHANNEL_NONE;
    }
    if (has_unknown_channel && has_named_channel)
        return MA_INVALID_ARGS;

    ma_backend selected_backend{};
    const ma_backend* backends = nullptr;
    ma_uint32 backend_count = 0;
    if (config_.backend) {
        selected_backend = *config_.backend;
        backends = &selected_backend;
        backend_count = 1;
    }

    auto result = ma_context_init(backends, backend_count, nullptr, &context_);
    if (result != MA_SUCCESS)
        return result;
    context_initialized_ = true;

    auto device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_f32;
    device_config.playback.channels = config_.contract.layout.size();
    device_config.playback.pChannelMap = has_unknown_channel ? nullptr : channel_map.data();
    device_config.sampleRate = config_.contract.sample_rate;
    device_config.periodSizeInFrames = config_.period_size_in_frames == 0 ? config_.contract.quantum
                                                                          : config_.period_size_in_frames;
    device_config.dataCallback = &playback_device::data_callback;
    device_config.pUserData = this;
    device_config.noFixedSizedCallback = MA_TRUE;
    device_config.noDisableDenormals = MA_TRUE;

    result = ma_device_init(&context_, &device_config, &device_);
    if (result != MA_SUCCESS) {
        ma_context_uninit(&context_);
        context_initialized_ = false;
        return result;
    }
    device_initialized_ = true;
    return MA_SUCCESS;
}

auto playback_device::start() noexcept -> ma_result {
    if (!device_initialized_)
        return MA_INVALID_OPERATION;
    return ma_device_start(&device_);
}

auto playback_device::stop() noexcept -> ma_result {
    if (!device_initialized_)
        return MA_INVALID_OPERATION;
    return ma_device_stop(&device_);
}

void playback_device::uninitialize() noexcept {
    if (device_initialized_) {
        ma_device_uninit(&device_);
        device_initialized_ = false;
    }
    if (context_initialized_) {
        ma_context_uninit(&context_);
        context_initialized_ = false;
    }
    renderer_.reset();
    active_plan_ = nullptr;
    completed_generation_.store(0, std::memory_order_relaxed);
    failed_generation_.store(0, std::memory_order_relaxed);
}

void playback_device::data_callback(ma_device* device, void* output, const void*, ma_uint32 frame_count) noexcept {
    auto* self = static_cast<playback_device*>(device->pUserData);
    self->render(output, frame_count);
}

void playback_device::render(void* output, ma_uint32 frame_count) noexcept {
    if (output == nullptr || frame_count == 0)
        return;

    // A terminal stream must still allow new publications. The finite adapter
    // itself deliberately stops calling its renderer after its cache is empty.
    if (renderer_.stream_finished() && exchange_.audio_acquire(active_plan_))
        renderer_.restart_stream();

    auto render_quantum = [this](process_context context, planar_view block) noexcept -> stream_result {
        exchange_.audio_acquire(active_plan_);
        if (active_plan_ != nullptr && active_plan_->stream_processing()) {
            if (exchange_.audio_drain_requested())
                active_plan_->begin_drain();
            return active_plan_->process_stream(context, block);
        }
        if (active_plan_ != nullptr)
            active_plan_->process(context, block);
        else
            clear(block);
        return {context.frame_count, stream_state::running};
    };

    const auto result = renderer_.pull_stream(
        interleaved_view{static_cast<float*>(output), config_.contract.layout, frame_count}, render_quantum);
    if (active_plan_ != nullptr) {
        if (result.state == stream_state::complete)
            completed_generation_.store(exchange_.audio_generation(), std::memory_order_release);
        else if (result.state == stream_state::failed)
            failed_generation_.store(exchange_.audio_generation(), std::memory_order_release);
    }
}

} // namespace miniaudio::cpp_engine
