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

    // Use only while rendering is stopped.
    void reset(std::uint64_t first_frame = 0) noexcept {
        available_ = 0;
        next_render_frame_ = first_frame;
        counters_ = {};
        cache_.clear();
    }

  private:
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
    std::uint64_t next_render_frame_{};
    render_counters counters_{};
};

} // namespace miniaudio::cpp_engine
