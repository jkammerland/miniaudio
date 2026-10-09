#pragma once

#include "render_plan.hpp"

#include <phonon.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace miniaudio::cpp_engine {

struct steam_audio_hrtf_render_state;

class steam_audio_hrtf_render_lease {
  public:
    steam_audio_hrtf_render_lease(const steam_audio_hrtf_render_lease&) = delete;
    auto operator=(const steam_audio_hrtf_render_lease&) -> steam_audio_hrtf_render_lease& = delete;
    steam_audio_hrtf_render_lease(steam_audio_hrtf_render_lease&& other) noexcept;
    auto operator=(steam_audio_hrtf_render_lease&&) -> steam_audio_hrtf_render_lease& = delete;
    ~steam_audio_hrtf_render_lease();

    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }

  private:
    friend class steam_audio_hrtf;
    friend class steam_audio_binaural_node;

    explicit steam_audio_hrtf_render_lease(steam_audio_hrtf_render_state* state) noexcept;

    steam_audio_hrtf_render_state* state_{};
};

struct direction3 {
    float x{};
    float y{};
    float z{-1.0f};
};

class steam_audio_hrtf {
  public:
    steam_audio_hrtf(IPLContext context, IPLAudioSettings audio_settings, const IPLHRTFSettings& settings);
    steam_audio_hrtf(const steam_audio_hrtf&) = delete;
    auto operator=(const steam_audio_hrtf&) -> steam_audio_hrtf& = delete;
    ~steam_audio_hrtf();

    [[nodiscard]] auto audio_settings() const noexcept -> IPLAudioSettings { return audio_settings_; }
    [[nodiscard]] auto native_handle() const noexcept -> IPLHRTF { return hrtf_; }
    [[nodiscard]] auto try_acquire_render() const noexcept -> steam_audio_hrtf_render_lease;
    [[nodiscard]] auto suppressed_overlap_count() const noexcept -> std::uint32_t;

  private:
    friend class steam_audio_binaural_node;

    IPLAudioSettings audio_settings_{};
    IPLHRTF hrtf_{};
    std::shared_ptr<steam_audio_hrtf_render_state> render_state_;
};

struct steam_audio_binaural_config {
    channel_layout input_layout;
    IPLAudioSettings audio_settings{};
    IPLContext context{};
    const steam_audio_hrtf* hrtf{};
    IPLHRTFInterpolation interpolation{IPL_HRTFINTERPOLATION_NEAREST};
    float dry_wet{1.0f};
};

class steam_audio_binaural_node final : public node {
  public:
    explicit steam_audio_binaural_node(const steam_audio_binaural_config& config);
    steam_audio_binaural_node(const steam_audio_binaural_node&) = delete;
    auto operator=(const steam_audio_binaural_node&) -> steam_audio_binaural_node& = delete;
    ~steam_audio_binaural_node() override;

    [[nodiscard]] auto accepts_contract(const stream_contract& contract) const noexcept -> bool override {
        return contract.sample_rate == static_cast<std::uint32_t>(audio_settings_.samplingRate) &&
               contract.quantum == static_cast<std::uint32_t>(audio_settings_.frameSize);
    }
    [[nodiscard]] auto output_layout() const noexcept -> const channel_layout& override { return output_layout_; }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return 1; }
    [[nodiscard]] auto accepts_input(std::uint32_t index, const channel_layout& layout) const noexcept -> bool override {
        return index == 0 && layout == input_layout_;
    }
    void process(process_context context, std::span<const const_planar_view> inputs, planar_view output) noexcept override;

    // Single control-domain publisher; the render domain is the only consumer.
    [[nodiscard]] auto set_direction(direction3 direction) noexcept -> bool;
    [[nodiscard]] auto set_dry_wet(float dry_wet) noexcept -> bool;

  private:
    static constexpr std::uint32_t direction_index_mask = 0x3U;
    static constexpr std::uint32_t direction_dirty = 0x80000000U;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    [[nodiscard]] auto consume_direction() noexcept -> direction3;

    channel_layout input_layout_;
    channel_layout output_layout_{channel_layout::stereo()};
    IPLAudioSettings audio_settings_{};
    IPLContext context_{};
    IPLHRTF hrtf_{};
    std::shared_ptr<steam_audio_hrtf_render_state> hrtf_render_state_;
    IPLBinauralEffect effect_{};
    IPLHRTFInterpolation interpolation_{};
    std::array<direction3, 3> directions_{};
    std::uint32_t front_direction_{};
    std::uint32_t back_direction_{2};
    std::atomic<std::uint32_t> middle_direction_{1};
    std::atomic<std::uint32_t> dry_wet_bits_{};
    float applied_dry_wet_{1.0f};
};

} // namespace miniaudio::cpp_engine
