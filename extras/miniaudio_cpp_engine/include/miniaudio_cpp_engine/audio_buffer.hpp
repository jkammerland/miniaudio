#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace miniaudio::cpp_engine {

inline constexpr std::size_t max_channels = 32;
inline constexpr std::size_t audio_buffer_alignment = 64;

enum class channel_role : std::uint8_t {
    unknown,
    mono,
    front_left,
    front_right,
    front_center,
    low_frequency,
    back_left,
    back_right,
    side_left,
    side_right,
};

class channel_layout {
  public:
    channel_layout() = default;

    channel_layout(std::initializer_list<channel_role> roles) {
        if (roles.size() == 0 || roles.size() > max_channels)
            throw std::invalid_argument("a channel layout must contain 1..32 roles");

        size_ = static_cast<std::uint8_t>(roles.size());
        std::copy(roles.begin(), roles.end(), roles_.begin());
        for (std::size_t index = 0; index < roles.size(); ++index) {
            if (roles_[index] == channel_role::mono && roles.size() != 1)
                throw std::invalid_argument("the mono role must be the only channel");
            if (roles_[index] == channel_role::unknown)
                continue;
            for (std::size_t earlier = 0; earlier < index; ++earlier) {
                if (roles_[earlier] == roles_[index])
                    throw std::invalid_argument("named channel roles must be unique");
            }
        }
    }

    [[nodiscard]] static auto mono() -> channel_layout { return {channel_role::mono}; }
    [[nodiscard]] static auto stereo() -> channel_layout {
        return {channel_role::front_left, channel_role::front_right};
    }

    [[nodiscard]] constexpr auto size() const noexcept -> std::uint32_t { return size_; }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }

    [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept -> channel_role {
        assert(index < size_);
        return roles_[index];
    }

    friend constexpr auto operator==(const channel_layout&, const channel_layout&) noexcept -> bool = default;

  private:
    std::array<channel_role, max_channels> roles_{};
    std::uint8_t size_{};
};

template <class Sample> class basic_planar_view {
    template <class> friend class basic_planar_view;

  public:
    using sample_type = Sample;
    using pointer = Sample*;

    basic_planar_view() = default;

    basic_planar_view(channel_layout layout, std::uint32_t frames, std::span<pointer const> planes) noexcept
        : layout_(layout), frames_(frames) {
        assert(!layout.empty());
        assert(planes.size() >= layout.size());
        std::copy_n(planes.begin(), layout.size(), planes_.begin());
    }

    template <class Other>
        requires(std::is_const_v<Sample> && std::same_as<std::remove_const_t<Sample>, Other>)
    basic_planar_view(const basic_planar_view<Other>& other) noexcept : layout_(other.layout_), frames_(other.frames_) {
        std::copy_n(other.planes_.begin(), layout_.size(), planes_.begin());
    }

    [[nodiscard]] auto layout() const noexcept -> const channel_layout& { return layout_; }
    [[nodiscard]] auto channels() const noexcept -> std::uint32_t { return layout_.size(); }
    [[nodiscard]] auto frames() const noexcept -> std::uint32_t { return frames_; }

    [[nodiscard]] auto channel(std::size_t index) const noexcept -> std::span<Sample> {
        assert(index < channels());
        return {planes_[index], frames_};
    }

    [[nodiscard]] auto channel_data(std::size_t index) const noexcept -> pointer {
        assert(index < channels());
        return planes_[index];
    }

  private:
    channel_layout layout_{};
    std::uint32_t frames_{};
    std::array<pointer, max_channels> planes_{};
};

using planar_view = basic_planar_view<float>;
using const_planar_view = basic_planar_view<const float>;

template <class Sample> class basic_interleaved_view {
  public:
    basic_interleaved_view() = default;
    basic_interleaved_view(Sample* data, channel_layout layout, std::uint32_t frames) noexcept
        : data_(data), layout_(layout), frames_(frames) {
        assert(!layout.empty());
        assert(data != nullptr || frames == 0);
    }

    [[nodiscard]] auto data() const noexcept -> Sample* { return data_; }
    [[nodiscard]] auto layout() const noexcept -> const channel_layout& { return layout_; }
    [[nodiscard]] auto channels() const noexcept -> std::uint32_t { return layout_.size(); }
    [[nodiscard]] auto frames() const noexcept -> std::uint32_t { return frames_; }

  private:
    Sample* data_{};
    channel_layout layout_{};
    std::uint32_t frames_{};
};

using interleaved_view = basic_interleaved_view<float>;
using const_interleaved_view = basic_interleaved_view<const float>;

class planar_buffer {
  public:
    planar_buffer() = default;

    planar_buffer(channel_layout layout, std::uint32_t frames) : layout_(layout), frames_(frames) {
        if (layout.empty() || frames == 0)
            throw std::invalid_argument("a planar buffer requires a layout and at least one frame");

        const auto channels = static_cast<std::size_t>(layout.size());
        if (frames > std::numeric_limits<std::size_t>::max() / channels / sizeof(float))
            throw std::length_error("planar buffer size overflows size_t");

        sample_count_ = channels * frames;
        samples_ = static_cast<float*>(
            ::operator new[](sample_count_ * sizeof(float), std::align_val_t{audio_buffer_alignment}));
        std::fill_n(samples_, sample_count_, 0.0f);
    }

    planar_buffer(const planar_buffer&) = delete;
    auto operator=(const planar_buffer&) -> planar_buffer& = delete;

    planar_buffer(planar_buffer&& other) noexcept { swap(other); }

    auto operator=(planar_buffer&& other) noexcept -> planar_buffer& {
        if (this != &other) {
            planar_buffer temporary{std::move(other)};
            swap(temporary);
        }
        return *this;
    }

    ~planar_buffer() {
        if (samples_ != nullptr)
            ::operator delete[](samples_, std::align_val_t{audio_buffer_alignment});
    }

    [[nodiscard]] auto layout() const noexcept -> const channel_layout& { return layout_; }
    [[nodiscard]] auto channels() const noexcept -> std::uint32_t { return layout_.size(); }
    [[nodiscard]] auto frames() const noexcept -> std::uint32_t { return frames_; }
    [[nodiscard]] auto data() noexcept -> float* { return samples_; }
    [[nodiscard]] auto data() const noexcept -> const float* { return samples_; }

    [[nodiscard]] auto view() noexcept -> planar_view {
        std::array<float*, max_channels> planes{};
        for (std::uint32_t channel = 0; channel < channels(); ++channel)
            planes[channel] = samples_ + static_cast<std::size_t>(channel) * frames_;
        return {layout_, frames_, std::span<float* const>{planes.data(), channels()}};
    }

    [[nodiscard]] auto view() const noexcept -> const_planar_view {
        std::array<const float*, max_channels> planes{};
        for (std::uint32_t channel = 0; channel < channels(); ++channel)
            planes[channel] = samples_ + static_cast<std::size_t>(channel) * frames_;
        return {layout_, frames_, std::span<const float* const>{planes.data(), channels()}};
    }

    void clear() noexcept { std::fill_n(samples_, sample_count_, 0.0f); }

    void swap(planar_buffer& other) noexcept {
        using std::swap;
        swap(layout_, other.layout_);
        swap(frames_, other.frames_);
        swap(sample_count_, other.sample_count_);
        swap(samples_, other.samples_);
    }

  private:
    channel_layout layout_{};
    std::uint32_t frames_{};
    std::size_t sample_count_{};
    float* samples_{};
};

inline void clear(planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        std::fill(output.channel(channel).begin(), output.channel(channel).end(), 0.0f);
}

inline void copy(const_planar_view input, planar_view output) noexcept {
    assert(input.layout() == output.layout());
    assert(input.frames() == output.frames());
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        std::copy(input.channel(channel).begin(), input.channel(channel).end(), output.channel(channel).begin());
}

inline void deinterleave(const_interleaved_view input, planar_view output) noexcept {
    assert(input.layout() == output.layout());
    assert(input.frames() == output.frames());
    for (std::uint32_t frame = 0; frame < input.frames(); ++frame) {
        for (std::uint32_t channel = 0; channel < input.channels(); ++channel)
            output.channel_data(channel)[frame] = input.data()[static_cast<std::size_t>(frame) * input.channels() + channel];
    }
}

inline void interleave_frames(const_planar_view input, std::uint32_t input_offset, interleaved_view output,
                              std::uint32_t output_offset, std::uint32_t frame_count) noexcept {
    assert(input.layout() == output.layout());
    assert(input_offset <= input.frames() && frame_count <= input.frames() - input_offset);
    assert(output_offset <= output.frames() && frame_count <= output.frames() - output_offset);

    auto* destination = output.data() + static_cast<std::size_t>(output_offset) * output.channels();
    if (output.channels() == 1) {
        std::copy_n(input.channel_data(0) + input_offset, frame_count, destination);
        return;
    }
    if (output.channels() == 2) {
        const auto* left = input.channel_data(0) + input_offset;
        const auto* right = input.channel_data(1) + input_offset;
        for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
            destination[static_cast<std::size_t>(frame) * 2] = left[frame];
            destination[static_cast<std::size_t>(frame) * 2 + 1] = right[frame];
        }
        return;
    }

    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
            destination[static_cast<std::size_t>(frame) * output.channels() + channel] =
                input.channel_data(channel)[input_offset + frame];
    }
}

inline void interleave(const_planar_view input, interleaved_view output) noexcept {
    assert(input.frames() == output.frames());
    interleave_frames(input, 0, output, 0, output.frames());
}

} // namespace miniaudio::cpp_engine
