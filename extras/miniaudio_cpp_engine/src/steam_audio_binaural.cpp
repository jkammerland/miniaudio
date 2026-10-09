#include "miniaudio_cpp_engine/steam_audio_binaural.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace miniaudio::cpp_engine {

namespace {
auto valid_audio_settings(IPLAudioSettings settings) noexcept -> bool {
    // Steam Audio's overlap window is undefined for frame sizes 4..7. This
    // adapter chooses a conservative useful minimum instead of exposing that
    // upstream edge case.
    return settings.samplingRate > 0 && settings.frameSize >= 8;
}
} // namespace

steam_audio_hrtf::steam_audio_hrtf(IPLContext context, IPLAudioSettings audio_settings,
                                   const IPLHRTFSettings& settings)
    : audio_settings_(audio_settings) {
    if (context == nullptr || !valid_audio_settings(audio_settings_))
        throw std::invalid_argument("invalid Steam Audio HRTF configuration");
    auto copied_settings = settings;
    if (iplHRTFCreate(context, &audio_settings_, &copied_settings, &hrtf_) != IPL_STATUS_SUCCESS)
        throw std::runtime_error("could not create Steam Audio HRTF");
}

steam_audio_hrtf::~steam_audio_hrtf() { iplHRTFRelease(&hrtf_); }

steam_audio_binaural_node::steam_audio_binaural_node(const steam_audio_binaural_config& config)
    : input_layout_(config.input_layout), audio_settings_(config.audio_settings), interpolation_(config.interpolation),
      applied_dry_wet_(config.dry_wet) {
    if ((input_layout_ != channel_layout::mono() && input_layout_ != channel_layout::stereo()) ||
        !valid_audio_settings(audio_settings_) || config.context == nullptr || config.hrtf == nullptr ||
        config.hrtf->audio_settings().samplingRate != audio_settings_.samplingRate ||
        config.hrtf->audio_settings().frameSize != audio_settings_.frameSize ||
        !std::isfinite(config.dry_wet) || config.dry_wet < 0.0f || config.dry_wet > 1.0f ||
        (interpolation_ != IPL_HRTFINTERPOLATION_NEAREST && interpolation_ != IPL_HRTFINTERPOLATION_BILINEAR)) {
        throw std::invalid_argument("invalid Steam Audio binaural configuration");
    }

    context_ = iplContextRetain(config.context);
    hrtf_ = iplHRTFRetain(config.hrtf->native_handle());
    if (context_ == nullptr || hrtf_ == nullptr) {
        iplHRTFRelease(&hrtf_);
        iplContextRelease(&context_);
        throw std::runtime_error("could not retain Steam Audio context or HRTF");
    }

    IPLBinauralEffectSettings effect_settings{};
    effect_settings.hrtf = hrtf_;
    if (iplBinauralEffectCreate(context_, &audio_settings_, &effect_settings, &effect_) != IPL_STATUS_SUCCESS) {
        iplHRTFRelease(&hrtf_);
        iplContextRelease(&context_);
        throw std::runtime_error("could not create Steam Audio binaural effect");
    }

    directions_.fill(direction3{});
    dry_wet_bits_.store(std::bit_cast<std::uint32_t>(config.dry_wet), std::memory_order_relaxed);
}

steam_audio_binaural_node::~steam_audio_binaural_node() {
    iplBinauralEffectRelease(&effect_);
    iplHRTFRelease(&hrtf_);
    iplContextRelease(&context_);
}

auto steam_audio_binaural_node::set_direction(direction3 direction) noexcept -> bool {
    const auto x = static_cast<double>(direction.x);
    const auto y = static_cast<double>(direction.y);
    const auto z = static_cast<double>(direction.z);
    const auto length = std::hypot(x, y, z);
    if (!std::isfinite(length) || length == 0.0)
        return false;
    direction.x = static_cast<float>(x / length);
    direction.y = static_cast<float>(y / length);
    direction.z = static_cast<float>(z / length);
    directions_[back_direction_] = direction;
    const auto previous = middle_direction_.exchange(back_direction_ | direction_dirty, std::memory_order_acq_rel);
    back_direction_ = previous & direction_index_mask;
    return true;
}

auto steam_audio_binaural_node::set_dry_wet(float dry_wet) noexcept -> bool {
    if (!std::isfinite(dry_wet) || dry_wet < 0.0f || dry_wet > 1.0f)
        return false;
    dry_wet_bits_.store(std::bit_cast<std::uint32_t>(dry_wet), std::memory_order_release);
    return true;
}

auto steam_audio_binaural_node::consume_direction() noexcept -> direction3 {
    auto middle = middle_direction_.load(std::memory_order_acquire);
    if ((middle & direction_dirty) != 0) {
        middle = middle_direction_.exchange(front_direction_, std::memory_order_acq_rel);
        front_direction_ = middle & direction_index_mask;
    }
    return directions_[front_direction_];
}

void steam_audio_binaural_node::process(process_context context, std::span<const const_planar_view> inputs,
                                        planar_view output) noexcept {
    if (inputs.size() != 1 || inputs.front().layout() != input_layout_ ||
        inputs.front().frames() != static_cast<std::uint32_t>(audio_settings_.frameSize) ||
        context.frame_count != static_cast<std::uint32_t>(audio_settings_.frameSize) ||
        context.sample_rate != static_cast<std::uint32_t>(audio_settings_.samplingRate) ||
        output.layout() != output_layout_ || output.frames() != static_cast<std::uint32_t>(audio_settings_.frameSize)) {
        clear(output);
        return;
    }

    const auto input = inputs.front();
    std::array<float*, 2> input_planes{};
    std::array<float*, 2> output_planes{};
    for (std::uint32_t channel = 0; channel < input.channels(); ++channel)
        input_planes[channel] = const_cast<float*>(input.channel_data(channel));
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        output_planes[channel] = output.channel_data(channel);

    const auto direction = consume_direction();
    IPLBinauralEffectParams parameters{};
    parameters.direction = {direction.x, direction.y, direction.z};
    parameters.interpolation = interpolation_;
    parameters.spatialBlend = 1.0f;
    parameters.hrtf = hrtf_;

    IPLAudioBuffer input_buffer{};
    input_buffer.numChannels = static_cast<IPLint32>(input.channels());
    input_buffer.numSamples = audio_settings_.frameSize;
    input_buffer.data = input_planes.data();

    IPLAudioBuffer output_buffer{};
    output_buffer.numChannels = 2;
    output_buffer.numSamples = audio_settings_.frameSize;
    output_buffer.data = output_planes.data();
    static_cast<void>(iplBinauralEffectApply(effect_, &parameters, &input_buffer, &output_buffer));

    const auto target = std::bit_cast<float>(dry_wet_bits_.load(std::memory_order_acquire));
    if (applied_dry_wet_ != 1.0f || target != 1.0f) {
        for (std::uint32_t frame = 0; frame < context.frame_count; ++frame) {
            const auto wet = frame + 1 == context.frame_count
                                 ? target
                                 : applied_dry_wet_ + (target - applied_dry_wet_) *
                                                            static_cast<float>(frame + 1) /
                                                            static_cast<float>(context.frame_count);
            for (std::uint32_t ear = 0; ear < 2; ++ear) {
                const auto dry = input.channel_data(input.channels() == 1 ? 0 : ear)[frame];
                output.channel_data(ear)[frame] = (1.0f - wet) * dry + wet * output.channel_data(ear)[frame];
            }
        }
    }
    applied_dry_wet_ = target;
}

} // namespace miniaudio::cpp_engine
