#include "miniaudio_cpp_engine/quantum_adapter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace miniaudio::cpp_engine;
using clock_type = std::chrono::steady_clock;

const stream_contract contract{channel_layout::stereo(), 48'000, 256};
constexpr std::array<std::uint32_t, 9> request_pattern{17, 64, 127, 480, 31, 256, 95, 511, 8};

auto source_sample(std::uint64_t absolute_frame, std::uint32_t channel) noexcept -> float {
    return static_cast<float>(absolute_frame & 1023U) * 0.001f + static_cast<float>(channel) * 0.25f;
}

void render_block(process_context context, planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
            output.channel_data(channel)[frame] = source_sample(context.first_frame + frame, channel);
    }
}

void render_interleaved_block(process_context context, std::span<float> output) noexcept {
    for (std::uint32_t frame = 0; frame < context.frame_count; ++frame) {
        for (std::uint32_t channel = 0; channel < contract.layout.size(); ++channel) {
            output[static_cast<std::size_t>(frame) * contract.layout.size() + channel] =
                source_sample(context.first_frame + frame, channel);
        }
    }
}

void make_observable(const float* output) noexcept {
    asm volatile("" : : "g"(output) : "memory");
}

class interleaved_shifting_cache {
  public:
    void pull(interleaved_view output) noexcept {
        std::uint32_t destination_offset = 0;
        while (destination_offset < output.frames()) {
            if (available_ == 0) {
                render_interleaved_block({next_frame_, contract.quantum, contract.sample_rate}, cache_);
                next_frame_ += contract.quantum;
                available_ = contract.quantum;
            }

            const auto count = std::min(output.frames() - destination_offset, available_);
            const auto samples = static_cast<std::size_t>(count) * output.channels();
            std::memcpy(output.data() + static_cast<std::size_t>(destination_offset) * output.channels(), cache_.data(),
                        samples * sizeof(float));
            available_ -= count;
            if (available_ != 0) {
                std::memmove(cache_.data(), cache_.data() + samples,
                             static_cast<std::size_t>(available_) * output.channels() * sizeof(float));
            }
            destination_offset += count;
        }
    }

  private:
    std::array<float, 2 * 256> cache_{};
    std::uint32_t available_{};
    std::uint64_t next_frame_{};
};

class shifting_cache {
  public:
    shifting_cache() : cache_(contract.layout, contract.quantum) {}

    void pull(interleaved_view output) noexcept {
        std::uint32_t destination_offset = 0;
        while (destination_offset < output.frames()) {
            if (available_ == 0) {
                render_block({next_frame_, contract.quantum, contract.sample_rate}, cache_.view());
                next_frame_ += contract.quantum;
                available_ = contract.quantum;
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
    planar_buffer cache_;
    std::uint32_t available_{};
    std::uint64_t next_frame_{};
};

class cursor_cache {
  public:
    cursor_cache() : adapter_(contract) {}

    void pull(interleaved_view output) noexcept {
        adapter_.pull(output, [](process_context context, planar_view block) noexcept { render_block(context, block); });
    }

  private:
    quantum_output_adapter adapter_;
};

struct measurement {
    double nanoseconds_per_frame{};
    double checksum{};
};

template <class Adapter, class Pull>
auto measure(std::size_t repetitions, Pull pull) -> measurement {
    Adapter adapter;
    const auto maximum = *std::max_element(request_pattern.begin(), request_pattern.end());
    std::vector<float> output(static_cast<std::size_t>(maximum) * contract.layout.size());
    std::uint64_t frames = 0;
    double checksum = 0.0;

    const auto started = clock_type::now();
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto request : request_pattern) {
            pull(adapter, interleaved_view{output.data(), contract.layout, request});
            make_observable(output.data());
            checksum += output[0] + output[static_cast<std::size_t>(request) * 2 - 1];
            frames += request;
        }
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(clock_type::now() - started).count();
    return {elapsed / static_cast<double>(frames), checksum};
}

void report(std::string_view name, std::vector<measurement> values) {
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return left.nanoseconds_per_frame < right.nanoseconds_per_frame;
    });
    std::cout << name << ",median_ns_per_frame=" << values[values.size() / 2].nanoseconds_per_frame
              << ",min=" << values.front().nanoseconds_per_frame
              << ",max=" << values.back().nanoseconds_per_frame
              << ",checksum=" << values[values.size() / 2].checksum << '\n';
}

auto median(std::vector<double> values) -> double {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

template <class Adapter, class Pull> auto collect_output(Pull pull) -> std::vector<float> {
    Adapter adapter;
    const auto maximum = *std::max_element(request_pattern.begin(), request_pattern.end());
    std::vector<float> block(static_cast<std::size_t>(maximum) * contract.layout.size());
    std::vector<float> result;
    for (std::size_t repetition = 0; repetition < 4; ++repetition) {
        for (const auto request : request_pattern) {
            pull(adapter, interleaved_view{block.data(), contract.layout, request});
            result.insert(result.end(), block.begin(),
                          block.begin() + static_cast<std::ptrdiff_t>(request * contract.layout.size()));
        }
    }
    return result;
}
} // namespace

int main() {
    constexpr std::size_t samples = 21;
    constexpr std::size_t repetitions = 50'000;
    std::cout << std::fixed << std::setprecision(4);

    const auto interleaved_pull = [](interleaved_shifting_cache& cache, interleaved_view output) { cache.pull(output); };
    const auto shifted_pull = [](shifting_cache& cache, interleaved_view output) { cache.pull(output); };
    const auto cursor_pull = [](cursor_cache& cache, interleaved_view output) { cache.pull(output); };

    const auto interleaved_reference = collect_output<interleaved_shifting_cache>(interleaved_pull);
    const auto planar_shift_reference = collect_output<shifting_cache>(shifted_pull);
    const auto planar_cursor_reference = collect_output<cursor_cache>(cursor_pull);
    if (interleaved_reference != planar_shift_reference || planar_shift_reference != planar_cursor_reference) {
        std::cerr << "cache variants failed the complete-output comparison\n";
        return 1;
    }

    static_cast<void>(measure<interleaved_shifting_cache>(1'000, interleaved_pull));
    static_cast<void>(measure<shifting_cache>(1'000, shifted_pull));
    static_cast<void>(measure<cursor_cache>(1'000, cursor_pull));

    std::vector<measurement> interleaved;
    std::vector<measurement> shifted;
    std::vector<measurement> cursor;
    std::vector<double> planar_shift_over_cursor;
    std::vector<double> interleaved_shift_over_cursor;
    interleaved.reserve(samples);
    shifted.reserve(samples);
    cursor.reserve(samples);
    planar_shift_over_cursor.reserve(samples);
    interleaved_shift_over_cursor.reserve(samples);
    std::cout << "sample,order,interleaved_shift_ns_per_frame,planar_shift_ns_per_frame,planar_cursor_ns_per_frame,checksum\n";
    for (std::size_t sample = 0; sample < samples; ++sample) {
        measurement interleaved_sample;
        measurement shifted_sample;
        measurement cursor_sample;
        const char* order = nullptr;
        switch (sample % 3) {
            case 0:
                order = "interleaved-planar_shift-planar_cursor";
                interleaved_sample = measure<interleaved_shifting_cache>(repetitions, interleaved_pull);
                shifted_sample = measure<shifting_cache>(repetitions, shifted_pull);
                cursor_sample = measure<cursor_cache>(repetitions, cursor_pull);
                break;
            case 1:
                order = "planar_shift-planar_cursor-interleaved";
                shifted_sample = measure<shifting_cache>(repetitions, shifted_pull);
                cursor_sample = measure<cursor_cache>(repetitions, cursor_pull);
                interleaved_sample = measure<interleaved_shifting_cache>(repetitions, interleaved_pull);
                break;
            default:
                order = "planar_cursor-interleaved-planar_shift";
                cursor_sample = measure<cursor_cache>(repetitions, cursor_pull);
                interleaved_sample = measure<interleaved_shifting_cache>(repetitions, interleaved_pull);
                shifted_sample = measure<shifting_cache>(repetitions, shifted_pull);
                break;
        }
        if (interleaved_sample.checksum != shifted_sample.checksum || shifted_sample.checksum != cursor_sample.checksum) {
            std::cerr << "cache variants produced different output\n";
            return 1;
        }
        std::cout << sample << ',' << order << ',' << interleaved_sample.nanoseconds_per_frame << ','
                  << shifted_sample.nanoseconds_per_frame << ',' << cursor_sample.nanoseconds_per_frame << ','
                  << cursor_sample.checksum << '\n';
        planar_shift_over_cursor.push_back(shifted_sample.nanoseconds_per_frame / cursor_sample.nanoseconds_per_frame);
        interleaved_shift_over_cursor.push_back(interleaved_sample.nanoseconds_per_frame /
                                                cursor_sample.nanoseconds_per_frame);
        interleaved.push_back(interleaved_sample);
        shifted.push_back(shifted_sample);
        cursor.push_back(cursor_sample);
    }

    report("interleaved_shift_cache", std::move(interleaved));
    report("planar_shift_cache", std::move(shifted));
    report("planar_cursor_cache", std::move(cursor));
    std::cout << "paired_planar_shift_over_cursor_median=" << median(std::move(planar_shift_over_cursor)) << '\n';
    std::cout << "paired_interleaved_shift_over_cursor_median=" << median(std::move(interleaved_shift_over_cursor)) << '\n';
}
