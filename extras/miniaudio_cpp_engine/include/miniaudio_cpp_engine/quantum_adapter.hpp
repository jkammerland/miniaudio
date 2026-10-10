#pragma once

#include "render_plan.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace miniaudio::cpp_engine {

struct render_counters {
    std::uint64_t rendered_quanta{};
    std::uint64_t rendered_frames{};
    std::uint64_t delivered_frames{};
};

class quantum_output_adapter {
  public:
    explicit quantum_output_adapter(stream_contract contract)
        : contract_(contract), cache_(contract.layout, contract.quantum) {
        if (!contract.valid())
            throw std::invalid_argument("a quantum adapter requires a valid stream contract");
    }

    [[nodiscard]] auto contract() const noexcept -> const stream_contract& { return contract_; }
    [[nodiscard]] auto cached_frames() const noexcept -> std::uint32_t { return available_; }
    [[nodiscard]] auto counters() const noexcept -> render_counters { return counters_; }
    [[nodiscard]] auto stream_finished() const noexcept -> bool {
        return available_ == 0 && cache_state_ != stream_state::running;
    }

    // Render-domain operation after the previous stream cache has been consumed.
    // Keep the quantum timeline and counters when adopting a replacement plan.
    void restart_stream() noexcept {
        assert(available_ == 0);
        if (available_ != 0)
            return;
        cache_offset_ = 0;
        cache_state_ = stream_state::running;
    }

    template <class Render> void pull(planar_view output, Render&& render) noexcept {
        assert(output.layout() == contract_.layout);
        if (output.layout() != contract_.layout) {
            clear(output);
            return;
        }
        std::uint32_t destination_offset = 0;
        while (destination_offset < output.frames()) {
            ensure_cache(render);
            const auto count = std::min(output.frames() - destination_offset, available_);
            const auto cached_offset = contract_.quantum - available_;
            for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
                const auto source = cache_.view().channel(channel).subspan(cached_offset, count);
                auto destination = output.channel(channel).subspan(destination_offset, count);
                std::copy(source.begin(), source.end(), destination.begin());
            }
            available_ -= count;
            destination_offset += count;
        }
        counters_.delivered_frames += output.frames();
    }

    template <class Render> void pull(interleaved_view output, Render&& render) noexcept {
        assert(output.layout() == contract_.layout);
        if (output.layout() != contract_.layout) {
            std::fill_n(output.data(), static_cast<std::size_t>(output.frames()) * output.channels(), 0.0f);
            return;
        }
        std::uint32_t destination_offset = 0;
        while (destination_offset < output.frames()) {
            ensure_cache(render);
            const auto count = std::min(output.frames() - destination_offset, available_);
            const auto cached_offset = contract_.quantum - available_;
            interleave_frames(cache_.view(), cached_offset, output, destination_offset, count);
            available_ -= count;
            destination_offset += count;
        }
        counters_.delivered_frames += output.frames();
    }

    template <class Render> auto pull_stream(planar_view output, Render&& render) noexcept -> stream_result {
        clear(output);
        if (output.layout() != contract_.layout)
            return fail_stream();
        return pull_stream_impl(output.frames(), render,
            [&](std::uint32_t source_offset, std::uint32_t destination_offset, std::uint32_t count) noexcept {
                for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
                    const auto source = cache_.view().channel(channel).subspan(source_offset, count);
                    auto destination = output.channel(channel).subspan(destination_offset, count);
                    std::copy(source.begin(), source.end(), destination.begin());
                }
            });
    }

    template <class Render> auto pull_stream(interleaved_view output, Render&& render) noexcept -> stream_result {
        std::fill_n(output.data(), static_cast<std::size_t>(output.frames()) * output.channels(), 0.0f);
        if (output.layout() != contract_.layout)
            return fail_stream();
        return pull_stream_impl(output.frames(), render,
            [&](std::uint32_t source_offset, std::uint32_t destination_offset, std::uint32_t count) noexcept {
                interleave_frames(cache_.view(), source_offset, output, destination_offset, count);
            });
    }

    // Use only while rendering is stopped.
    void reset(std::uint64_t first_frame = 0) noexcept {
        available_ = 0;
        cache_offset_ = 0;
        cache_state_ = stream_state::running;
        next_render_frame_ = first_frame;
        counters_ = {};
        cache_.clear();
    }

  private:
    auto fail_stream() noexcept -> stream_result {
        available_ = 0;
        cache_offset_ = 0;
        cache_state_ = stream_state::failed;
        return {0, cache_state_};
    }

    template <class Render, class Copy>
    auto pull_stream_impl(std::uint32_t requested, Render& render, Copy&& copy_frames) noexcept -> stream_result {
        std::uint32_t delivered = 0;
        while (delivered < requested) {
            if (available_ == 0) {
                if (cache_state_ != stream_state::running)
                    break;
                const process_context context{next_render_frame_, contract_.quantum, contract_.sample_rate};
                const auto result = render(context, cache_.view());
                next_render_frame_ += contract_.quantum;
                ++counters_.rendered_quanta;
                counters_.rendered_frames += contract_.quantum;
                if (!valid_stream_result(result, contract_.quantum)) {
                    static_cast<void>(fail_stream());
                    break;
                }
                available_ = result.valid_frames;
                cache_offset_ = 0;
                cache_state_ = result.state;
                if (available_ == 0)
                    break;
            }
            const auto count = std::min(requested - delivered, available_);
            copy_frames(cache_offset_, delivered, count);
            cache_offset_ += count;
            available_ -= count;
            delivered += count;
        }
        counters_.delivered_frames += requested;
        return {delivered, stream_finished() ? cache_state_ : stream_state::running};
    }

    template <class Render> void ensure_cache(Render& render) noexcept {
        if (available_ != 0)
            return;

        const process_context context{next_render_frame_, contract_.quantum, contract_.sample_rate};
        render(context, cache_.view());
        next_render_frame_ += contract_.quantum;
        available_ = contract_.quantum;
        ++counters_.rendered_quanta;
        counters_.rendered_frames += contract_.quantum;
    }

    stream_contract contract_;
    planar_buffer cache_;
    std::uint32_t available_{};
    std::uint32_t cache_offset_{};
    stream_state cache_state_{stream_state::running};
    std::uint64_t next_render_frame_{};
    render_counters counters_{};
};

} // namespace miniaudio::cpp_engine
