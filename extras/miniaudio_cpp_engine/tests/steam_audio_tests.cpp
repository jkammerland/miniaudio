#include "miniaudio_cpp_engine/render_plan.hpp"
#include "miniaudio_cpp_engine/steam_audio_binaural.hpp"

#include <phonon.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
using namespace miniaudio::cpp_engine;

constexpr std::uint32_t frame_count = 256;
std::atomic<bool> track_steam_allocations{};
std::atomic<std::uint32_t> steam_allocation_count{};
std::atomic<bool> track_host_allocations{};
std::atomic<std::uint32_t> host_allocation_count{};

void record_host_allocation() noexcept {
    if (track_host_allocations.load(std::memory_order_relaxed))
        host_allocation_count.fetch_add(1, std::memory_order_relaxed);
}

auto allocate_for_host(std::size_t size) -> void* {
    if (auto* result = std::malloc(size == 0 ? 1 : size)) {
        record_host_allocation();
        return result;
    }
    throw std::bad_alloc{};
}

auto allocate_aligned_for_host(std::size_t size, std::size_t alignment) -> void* {
    void* result = nullptr;
#if defined(_MSC_VER)
    result = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    if (posix_memalign(&result, alignment, size == 0 ? alignment : size) != 0)
        result = nullptr;
#endif
    if (result == nullptr)
        throw std::bad_alloc{};
    record_host_allocation();
    return result;
}

void free_aligned_for_host(void* pointer) noexcept {
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

auto IPLCALL allocate_for_steam(IPLsize size, IPLsize alignment) -> void* {
    if (track_steam_allocations.load(std::memory_order_relaxed))
        steam_allocation_count.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    void* result = nullptr;
    return posix_memalign(&result, alignment, size) == 0 ? result : nullptr;
#endif
}

void IPLCALL free_for_steam(void* pointer) {
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
} // namespace

#if defined(MINIAUDIO_CPP_ENGINE_WRAP_C_ALLOCATIONS)
extern "C" {
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);
void* __real_aligned_alloc(std::size_t, std::size_t);
int __real_posix_memalign(void**, std::size_t, std::size_t);

void* __wrap_malloc(std::size_t size) {
    record_host_allocation();
    return __real_malloc(size);
}
void* __wrap_calloc(std::size_t count, std::size_t size) {
    record_host_allocation();
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* pointer, std::size_t size) {
    record_host_allocation();
    return __real_realloc(pointer, size);
}
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size) {
    record_host_allocation();
    return __real_aligned_alloc(alignment, size);
}
int __wrap_posix_memalign(void** result, std::size_t alignment, std::size_t size) {
    record_host_allocation();
    return __real_posix_memalign(result, alignment, size);
}
}
#endif

void* operator new(std::size_t size) { return allocate_for_host(size); }
void* operator new[](std::size_t size) { return allocate_for_host(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned_for_host(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned_for_host(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* pointer, std::align_val_t) noexcept { free_aligned_for_host(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { free_aligned_for_host(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { free_aligned_for_host(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned_for_host(pointer);
}

namespace {
using namespace miniaudio::cpp_engine;

auto source_sample(std::uint64_t absolute_frame, std::uint32_t channel) noexcept -> float {
    const auto period = channel == 0 ? 29U : 37U;
    const auto offset = channel == 0 ? 7U : 19U;
    return (static_cast<float>((absolute_frame * (channel + 3U) + offset) % period) -
            static_cast<float>(period / 2U)) *
           0.015625f;
}

void fill_stereo(void*, process_context context, planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
            output.channel_data(channel)[frame] = source_sample(context.first_frame + frame, channel);
    }
}

void fill_direct_input(std::uint64_t first_frame, std::array<std::array<float, frame_count>, 2>& input) {
    for (std::uint32_t channel = 0; channel < input.size(); ++channel) {
        for (std::uint32_t frame = 0; frame < frame_count; ++frame)
            input[channel][frame] = source_sample(first_frame + frame, channel);
    }
}

auto nearly_equal(float left, float right) noexcept -> bool {
    return std::abs(left - right) <= 2.0e-6f;
}

struct effect_owner {
    IPLBinauralEffect value{};
    ~effect_owner() { iplBinauralEffectRelease(&value); }
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

auto is_silent(const planar_buffer& buffer) noexcept -> bool {
    const auto view = buffer.view();
    for (std::uint32_t channel = 0; channel < buffer.channels(); ++channel) {
        for (const auto sample : view.channel(channel)) {
            if (sample != 0.0f)
                return false;
        }
    }
    return true;
}

auto buffers_match(const planar_buffer& left, const planar_buffer& right) noexcept -> bool {
    const auto left_view = left.view();
    const auto right_view = right.view();
    for (std::uint32_t channel = 0; channel < left.channels(); ++channel) {
        for (std::uint32_t frame = 0; frame < left.frames(); ++frame) {
            if (!nearly_equal(left_view.channel_data(channel)[frame], right_view.channel_data(channel)[frame]))
                return false;
        }
    }
    return true;
}

void verify_shared_hrtf_overlap_is_suppressed(IPLContext context) {
    IPLAudioSettings settings{};
    settings.samplingRate = 48'000;
    settings.frameSize = frame_count;
    IPLHRTFSettings hrtf_settings{};
    hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
    hrtf_settings.volume = 1.0f;

    steam_audio_hrtf shared_hrtf{context, settings, hrtf_settings};
    steam_audio_hrtf reference_hrtf{context, settings, hrtf_settings};
    steam_audio_binaural_node shared_node{steam_audio_binaural_config{
        channel_layout::mono(), settings, context, &shared_hrtf, IPL_HRTFINTERPOLATION_BILINEAR, 1.0f}};
    steam_audio_binaural_node reference_node{steam_audio_binaural_config{
        channel_layout::mono(), settings, context, &reference_hrtf, IPL_HRTFINTERPOLATION_BILINEAR, 1.0f}};
    require(shared_node.set_direction({-1.0f, 0.25f, -1.0f}) &&
                reference_node.set_direction({-1.0f, 0.25f, -1.0f}),
            "could not configure HRTF overlap test direction");

    planar_buffer input{channel_layout::mono(), frame_count};
    planar_buffer shared_output{channel_layout::stereo(), frame_count};
    planar_buffer reference_output{channel_layout::stereo(), frame_count};
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        input.view().channel_data(0)[frame] = source_sample(frame, 0);
        shared_output.view().channel_data(0)[frame] = 1.0f;
        shared_output.view().channel_data(1)[frame] = -1.0f;
    }
    const std::array<const_planar_view, 1> inputs{input.view()};
    const auto overlap_count_before = shared_hrtf.suppressed_overlap_count();

    {
        auto owner_lease = shared_hrtf.try_acquire_render();
        require(static_cast<bool>(owner_lease), "could not acquire the initial HRTF render lease");
        const auto steam_before = steam_allocation_count.load(std::memory_order_relaxed);
        const auto host_before = host_allocation_count.load(std::memory_order_relaxed);
        track_steam_allocations.store(true, std::memory_order_relaxed);
        track_host_allocations.store(true, std::memory_order_relaxed);
        shared_node.process({0, frame_count, 48'000}, inputs, shared_output.view());
        track_host_allocations.store(false, std::memory_order_relaxed);
        track_steam_allocations.store(false, std::memory_order_relaxed);
        require(steam_allocation_count.load(std::memory_order_relaxed) == steam_before &&
                    host_allocation_count.load(std::memory_order_relaxed) == host_before,
                "suppressed shared HRTF render allocated");
        require(is_silent(shared_output), "suppressed shared HRTF render did not clear its output");
        require(shared_hrtf.suppressed_overlap_count() == overlap_count_before + 1,
                "suppressed shared HRTF render was not counted exactly once");
    }

    shared_node.process({0, frame_count, 48'000}, inputs, shared_output.view());
    reference_node.process({0, frame_count, 48'000}, inputs, reference_output.view());
    require(!is_silent(reference_output), "HRTF overlap reference was silent");
    require(buffers_match(shared_output, reference_output),
            "shared HRTF did not recover after deterministic overlap suppression");
    require(shared_hrtf.suppressed_overlap_count() == overlap_count_before + 1,
            "accepted shared HRTF render changed the suppression count");
}

void run_tests(IPLContext context) {
#if defined(MINIAUDIO_CPP_ENGINE_WRAP_C_ALLOCATIONS)
    const auto interception_before = host_allocation_count.load(std::memory_order_relaxed);
    track_host_allocations.store(true, std::memory_order_relaxed);
    auto* c_allocation = std::malloc(64);
    if (c_allocation != nullptr)
        *static_cast<volatile unsigned char*>(c_allocation) = 0;
    track_host_allocations.store(false, std::memory_order_relaxed);
    std::free(c_allocation);
    require(c_allocation != nullptr &&
                host_allocation_count.load(std::memory_order_relaxed) > interception_before,
            "C allocation interception is inactive");
#endif

    IPLAudioSettings audio_settings{};
    audio_settings.samplingRate = 48'000;
    audio_settings.frameSize = frame_count;
    IPLHRTFSettings hrtf_settings{};
    hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
    hrtf_settings.volume = 1.0f;
    steam_audio_hrtf hrtf{context, audio_settings, hrtf_settings};
    verify_shared_hrtf_overlap_is_suppressed(context);

    auto rejected_short_frame = false;
    try {
        auto invalid_settings = audio_settings;
        invalid_settings.frameSize = 4;
        steam_audio_hrtf invalid{context, invalid_settings, hrtf_settings};
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        rejected_short_frame = true;
    }
    require(rejected_short_frame, "unsafe short Steam Audio frame was accepted");

    auto rejected_mismatched_hrtf = false;
    try {
        auto mismatched_settings = audio_settings;
        mismatched_settings.frameSize *= 2;
        steam_audio_binaural_node invalid{steam_audio_binaural_config{
            channel_layout::stereo(), mismatched_settings, context, &hrtf, IPL_HRTFINTERPOLATION_NEAREST, 1.0f}};
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        rejected_mismatched_hrtf = true;
    }
    require(rejected_mismatched_hrtf, "mismatched HRTF audio settings were accepted");

    {
        const stream_contract mono_contract{channel_layout::stereo(), 48'000, frame_count};
        render_plan_builder mono_builder{mono_contract};
        const auto mono_source = mono_builder.add(
            std::make_unique<callback_source_node>(channel_layout::mono(), nullptr, fill_stereo));
        const std::array mono_input{mono_source};
        const auto mono_effect = mono_builder.add(
            std::make_unique<steam_audio_binaural_node>(steam_audio_binaural_config{
                channel_layout::mono(), audio_settings, context, &hrtf, IPL_HRTFINTERPOLATION_NEAREST, 0.0f}),
            mono_input);
        auto mono_plan = std::move(mono_builder).compile(mono_effect);
        planar_buffer mono_output{mono_contract.layout, mono_contract.quantum};
        const auto steam_before = steam_allocation_count.load(std::memory_order_relaxed);
        const auto host_before = host_allocation_count.load(std::memory_order_relaxed);
        track_steam_allocations.store(true, std::memory_order_relaxed);
        track_host_allocations.store(true, std::memory_order_relaxed);
        mono_plan->process({0, mono_contract.quantum, mono_contract.sample_rate}, mono_output.view());
        track_host_allocations.store(false, std::memory_order_relaxed);
        track_steam_allocations.store(false, std::memory_order_relaxed);
        require(steam_allocation_count.load(std::memory_order_relaxed) == steam_before &&
                    host_allocation_count.load(std::memory_order_relaxed) == host_before,
                "mono Steam Audio render allocated");
        for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
            const auto expected = source_sample(frame, 0);
            require(nearly_equal(mono_output.view().channel_data(0)[frame], expected) &&
                        nearly_equal(mono_output.view().channel_data(1)[frame], expected),
                    "mono dry path did not duplicate its source");
        }
    }

    const stream_contract contract{channel_layout::stereo(), 48'000, frame_count};
    render_plan_builder builder{contract};
    const auto source = builder.add(
        std::make_unique<callback_source_node>(channel_layout::stereo(), nullptr, fill_stereo));
    auto binaural = std::make_unique<steam_audio_binaural_node>(steam_audio_binaural_config{
        channel_layout::stereo(), audio_settings, context, &hrtf, IPL_HRTFINTERPOLATION_NEAREST, 1.0f});
    auto* control = binaural.get();
    require(control->set_direction({3.0e-23f, 0.0f, 0.0f}), "small finite direction was rejected");
    require(control->set_direction({1.0e20f, 0.0f, 0.0f}), "large finite direction was rejected");
    require(!control->set_direction({0.0f, 0.0f, 0.0f}), "zero direction was accepted");
    require(!control->set_direction({std::numeric_limits<float>::infinity(), 0.0f, 0.0f}),
            "non-finite direction was accepted");
    require(control->set_direction({1.0f, 0.0f, -1.0f}), "ordinary direction was rejected");
    require(!control->set_dry_wet(-0.1f), "invalid dry/wet value was accepted");

    const std::array input{source};
    const auto effect = builder.add(std::move(binaural), input);
    auto plan = std::move(builder).compile(effect);

    IPLBinauralEffectSettings effect_settings{};
    effect_settings.hrtf = hrtf.native_handle();
    effect_owner reference_effect;
    require(iplBinauralEffectCreate(context, &audio_settings, &effect_settings, &reference_effect.value) ==
                IPL_STATUS_SUCCESS,
            "could not create direct Steam Audio reference effect");

    constexpr auto inverse_sqrt_two = 0.70710678118654752440f;
    IPLBinauralEffectParams parameters{};
    parameters.direction = {inverse_sqrt_two, 0.0f, -inverse_sqrt_two};
    parameters.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
    parameters.spatialBlend = 1.0f;
    parameters.hrtf = hrtf.native_handle();

    std::array<std::array<float, frame_count>, 2> direct_input{};
    std::array<std::array<float, frame_count>, 2> direct_output{};
    std::array<float*, 2> input_planes{direct_input[0].data(), direct_input[1].data()};
    std::array<float*, 2> output_planes{direct_output[0].data(), direct_output[1].data()};
    IPLAudioBuffer input_buffer{2, static_cast<IPLint32>(frame_count), input_planes.data()};
    IPLAudioBuffer output_buffer{2, static_cast<IPLint32>(frame_count), output_planes.data()};

    fill_direct_input(0, direct_input);
    {
        auto render_lease = hrtf.try_acquire_render();
        require(static_cast<bool>(render_lease), "could not acquire HRTF lease for direct reference render");
        static_cast<void>(iplBinauralEffectApply(reference_effect.value, &parameters, &input_buffer, &output_buffer));
    }
    planar_buffer actual{contract.layout, contract.quantum};
    const auto allocations_before_wet = steam_allocation_count.load(std::memory_order_relaxed);
    const auto host_allocations_before_wet = host_allocation_count.load(std::memory_order_relaxed);
    track_steam_allocations.store(true, std::memory_order_relaxed);
    track_host_allocations.store(true, std::memory_order_relaxed);
    plan->process({0, contract.quantum, contract.sample_rate}, actual.view());
    track_host_allocations.store(false, std::memory_order_relaxed);
    track_steam_allocations.store(false, std::memory_order_relaxed);
    require(steam_allocation_count.load(std::memory_order_relaxed) == allocations_before_wet &&
                host_allocation_count.load(std::memory_order_relaxed) == host_allocations_before_wet,
            "Steam Audio wet render allocated");

    auto wet_changed_audio = false;
    auto ears_differ = false;
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const auto sample = actual.view().channel_data(channel)[frame];
            require(std::isfinite(sample), "wet path produced a non-finite sample");
            require(nearly_equal(sample, direct_output[channel][frame]),
                    "wet path differs from direct Steam Audio output");
            wet_changed_audio = wet_changed_audio || !nearly_equal(sample, direct_input[channel][frame]);
        }
        ears_differ = ears_differ || !nearly_equal(actual.view().channel_data(0)[frame],
                                                    actual.view().channel_data(1)[frame]);
    }
    require(wet_changed_audio && ears_differ, "wet-path reference case was not acoustically discriminating");

    require(control->set_dry_wet(0.0f), "valid dry/wet update was rejected");
    fill_direct_input(frame_count, direct_input);
    {
        auto render_lease = hrtf.try_acquire_render();
        require(static_cast<bool>(render_lease), "could not reacquire HRTF lease for direct reference render");
        static_cast<void>(iplBinauralEffectApply(reference_effect.value, &parameters, &input_buffer, &output_buffer));
    }
    const auto allocations_before_ramp = steam_allocation_count.load(std::memory_order_relaxed);
    const auto host_allocations_before_ramp = host_allocation_count.load(std::memory_order_relaxed);
    track_steam_allocations.store(true, std::memory_order_relaxed);
    track_host_allocations.store(true, std::memory_order_relaxed);
    plan->process({frame_count, contract.quantum, contract.sample_rate}, actual.view());
    track_host_allocations.store(false, std::memory_order_relaxed);
    track_steam_allocations.store(false, std::memory_order_relaxed);
    require(steam_allocation_count.load(std::memory_order_relaxed) == allocations_before_ramp &&
                host_allocation_count.load(std::memory_order_relaxed) == host_allocations_before_ramp,
            "Steam Audio ramp render allocated");
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        const auto wet = frame + 1 == frame_count
                             ? 0.0f
                             : 1.0f - static_cast<float>(frame + 1) / static_cast<float>(frame_count);
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const auto expected = (1.0f - wet) * direct_input[channel][frame] +
                                  wet * direct_output[channel][frame];
            require(nearly_equal(actual.view().channel_data(channel)[frame], expected),
                    "dry/wet ramp differs from the specified blend");
        }
    }
}
} // namespace

int main() {
    IPLContextSettings context_settings{};
    context_settings.version = STEAMAUDIO_VERSION;
    context_settings.allocateCallback = allocate_for_steam;
    context_settings.freeCallback = free_for_steam;
    IPLContext context{};
    if (iplContextCreate(&context_settings, &context) != IPL_STATUS_SUCCESS) {
        std::cerr << "could not create Steam Audio context\n";
        return 1;
    }

    auto result = 0;
    try {
        run_tests(context);
        std::cout << "PASS steam_audio_planar_binaural_node\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL steam_audio_planar_binaural_node: " << error.what() << '\n';
        result = 1;
    }
    iplContextRelease(&context);
    return result;
}
