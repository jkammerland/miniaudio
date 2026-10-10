#include "miniaudio_cpp_engine/plan_exchange.hpp"
#include "miniaudio_cpp_engine/quantum_adapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
std::atomic<std::size_t> allocation_count{};
std::atomic<bool> track_allocations{};
std::atomic<int> allocations_until_failure{-1};

void record_allocation() noexcept {
    if (track_allocations.load(std::memory_order_relaxed))
        allocation_count.fetch_add(1, std::memory_order_relaxed);
}

void inject_failure_if_requested() {
    auto remaining = allocations_until_failure.load(std::memory_order_relaxed);
    if (remaining < 0)
        return;
    if (remaining == 0) {
        allocations_until_failure.store(-1, std::memory_order_relaxed);
        throw std::bad_alloc{};
    }
    allocations_until_failure.store(remaining - 1, std::memory_order_relaxed);
}

auto allocate(std::size_t size) -> void* {
    inject_failure_if_requested();
    if (auto* result = std::malloc(size == 0 ? 1 : size)) {
        record_allocation();
        return result;
    }
    throw std::bad_alloc{};
}

auto allocate_aligned(std::size_t size, std::size_t alignment) -> void* {
    inject_failure_if_requested();
    void* result = nullptr;
#if defined(_MSC_VER)
    result = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    if (posix_memalign(&result, alignment, size == 0 ? alignment : size) != 0)
        result = nullptr;
#endif
    if (result == nullptr)
        throw std::bad_alloc{};
    record_allocation();
    return result;
}

void deallocate_aligned(void* pointer) noexcept {
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
    record_allocation();
    return __real_malloc(size);
}

void* __wrap_calloc(std::size_t count, std::size_t size) {
    record_allocation();
    return __real_calloc(count, size);
}

void* __wrap_realloc(void* pointer, std::size_t size) {
    record_allocation();
    return __real_realloc(pointer, size);
}

void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size) {
    record_allocation();
    return __real_aligned_alloc(alignment, size);
}

int __wrap_posix_memalign(void** result, std::size_t alignment, std::size_t size) {
    record_allocation();
    return __real_posix_memalign(result, alignment, size);
}
}
#endif

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* pointer, std::align_val_t) noexcept { deallocate_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { deallocate_aligned(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { deallocate_aligned(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { deallocate_aligned(pointer); }

namespace {
using namespace miniaudio::cpp_engine;

struct source_state {
    float value{};
};

void fill(void* opaque, process_context, planar_view output) noexcept {
    const auto value = static_cast<source_state*>(opaque)->value;
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        std::fill(output.channel(channel).begin(), output.channel(channel).end(), value);
}

void invert(void*, process_context, const_planar_view input, planar_view output) noexcept {
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel) {
        for (std::uint32_t frame = 0; frame < output.frames(); ++frame)
            output.channel_data(channel)[frame] = -input.channel_data(channel)[frame];
    }
}

auto make_composed_plan(stream_contract contract, source_state& first, source_state& second)
    -> std::unique_ptr<render_plan> {
    render_plan_builder builder{contract};
    const auto first_source = builder.add(
        std::make_unique<callback_source_node>(contract.layout, &first, fill));
    const std::array gain_input{first_source};
    const auto gain = builder.add(std::make_unique<gain_node>(contract.layout, 0.5f), gain_input);
    const auto second_source = builder.add(
        std::make_unique<callback_source_node>(contract.layout, &second, fill));
    const std::array mixer_inputs{gain, second_source};
    const auto mixer = builder.add(std::make_unique<mixer_node>(contract.layout, 2), mixer_inputs);
    const std::array effect_input{mixer};
    const auto effect = builder.add(
        std::make_unique<callback_effect_node>(contract.layout, contract.layout, nullptr, invert), effect_input);
    return std::move(builder).compile(effect);
}

auto verify_publication_failure_preserves_candidate(stream_contract contract, source_state& first,
                                                    source_state& second, int failure_index) -> bool {
    plan_exchange exchange{contract};
    auto candidate = make_composed_plan(contract, first, second);
    allocations_until_failure.store(failure_index, std::memory_order_relaxed);
    auto threw = false;
    try {
        static_cast<void>(exchange.try_publish(candidate));
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    allocations_until_failure.store(-1, std::memory_order_relaxed);
    return threw && candidate != nullptr && !exchange.pending() && exchange.acknowledged_generation() == 0;
}

struct finite_state { std::uint32_t remaining{}; std::uint32_t calls{}; };
auto fill_finite(void* opaque, process_context, planar_view output) noexcept -> source_result {
    auto& state = *static_cast<finite_state*>(opaque);
    ++state.calls;
    const auto count = std::min(output.frames(), state.remaining);
    for (std::uint32_t channel = 0; channel < output.channels(); ++channel)
        std::fill_n(output.channel_data(channel), count, 0.5f);
    state.remaining -= count;
    return {count, state.remaining == 0};
}

auto make_stream_plan(stream_contract contract, finite_state& state) -> std::unique_ptr<render_plan> {
    render_plan_builder builder{contract};
    const auto source = builder.add(std::make_unique<finite_source_node>(contract.layout, &state, fill_finite));
    const std::array input{source};
    const auto gained = builder.add(std::make_unique<gain_node>(contract.layout, 0.5f), input);
    const std::array mixed{gained};
    const auto mixer = builder.add(std::make_unique<mixer_node>(contract.layout, 1), mixed);
    const std::array effect_input{mixer};
    const auto output = builder.add(std::make_unique<tail_free_effect_node>(contract.layout, contract.layout, nullptr, invert), effect_input);
    return std::move(builder).compile_stream(output);
}

auto verify_stream_render_allocations(stream_contract contract) -> bool {
    finite_state first{1024}, replacement{17};
    plan_exchange exchange{contract};
    auto plan = make_stream_plan(contract, first);
    if (exchange.try_publish(plan) != publish_status::published)
        return false;
    render_plan* active = nullptr;
    quantum_output_adapter adapter{contract};
    std::array<float, 2 * 127> output{};
    auto render = [&](process_context context, planar_view block) noexcept -> stream_result {
        exchange.audio_acquire(active);
        if (exchange.audio_drain_requested())
            active->begin_drain();
        return active->process_stream(context, block);
    };
    const auto before = allocation_count.load(std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    static_cast<void>(adapter.pull_stream(interleaved_view{output.data(), contract.layout, 1}, render));
    const auto drain = exchange.request_drain();
    const auto end = adapter.pull_stream(interleaved_view{output.data(), contract.layout, 127}, render);
    track_allocations.store(false, std::memory_order_relaxed);
    if (drain != drain_request_status::queued || end.state != stream_state::complete || first.calls != 1)
        return false;

    auto candidate = make_stream_plan(contract, replacement);
    if (exchange.try_publish(candidate) != publish_status::published)
        return false;
    track_allocations.store(true, std::memory_order_relaxed);
    exchange.audio_acquire(active);
    adapter.restart_stream();
    constexpr std::array<std::uint32_t, 5> requests{1, 17, 64, 95, 127};
    for (std::uint32_t index = 0; index < 10'000; ++index)
        static_cast<void>(adapter.pull_stream(interleaved_view{output.data(), contract.layout, requests[index % requests.size()]}, render));
    track_allocations.store(false, std::memory_order_relaxed);
    return allocation_count.load(std::memory_order_relaxed) == before && replacement.calls == 1 && adapter.stream_finished();
}
} // namespace

int main() {
#if defined(MINIAUDIO_CPP_ENGINE_WRAP_C_ALLOCATIONS)
    const auto interception_before = allocation_count.load(std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    auto* c_allocation = std::malloc(64);
    if (c_allocation != nullptr)
        *static_cast<volatile unsigned char*>(c_allocation) = 0;
    track_allocations.store(false, std::memory_order_relaxed);
    std::free(c_allocation);
    if (c_allocation == nullptr || allocation_count.load(std::memory_order_relaxed) == interception_before) {
        std::cerr << "C allocation interception is inactive\n";
        return 1;
    }
#endif

    const stream_contract contract{channel_layout::stereo(), 48'000, 64};
    source_state first{0.25f};
    source_state second{0.5f};
    auto plan = make_composed_plan(contract, first, second);
    plan_exchange exchange{contract};
    if (exchange.try_publish(plan) != publish_status::published)
        return 1;

    render_plan* active = nullptr;
    quantum_output_adapter adapter{contract};
    auto render = [&](process_context context, planar_view output) noexcept {
        exchange.audio_acquire(active);
        if (active)
            active->process(context, output);
        else
            clear(output);
    };
    std::array<float, 2 * 127> output{};

    const auto before = allocation_count.load(std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    adapter.pull(interleaved_view{output.data(), contract.layout, contract.quantum}, render);
    track_allocations.store(false, std::memory_order_relaxed);

    first.value = 0.75f;
    second.value = -0.25f;
    auto replacement = make_composed_plan(contract, first, second);
    if (exchange.try_publish(replacement) != publish_status::published)
        return 1;
    track_allocations.store(true, std::memory_order_relaxed);
    adapter.pull(interleaved_view{output.data(), contract.layout, contract.quantum}, render);

    constexpr std::array<std::uint32_t, 5> requests{1, 17, 64, 95, 127};
    for (std::uint64_t index = 0; index < 10'000; ++index) {
        const auto frames = requests[index % requests.size()];
        adapter.pull(interleaved_view{output.data(), contract.layout, frames}, render);
    }
    track_allocations.store(false, std::memory_order_relaxed);
    const auto after = allocation_count.load(std::memory_order_relaxed);
    if (after != before) {
        std::cerr << "render path allocated " << after - before << " times\n";
        return 1;
    }
    exchange.reset_after_stop();

    if (!verify_publication_failure_preserves_candidate(contract, first, second, 0) ||
        !verify_publication_failure_preserves_candidate(contract, first, second, 1)) {
        std::cerr << "failed plan publication consumed its candidate\n";
        return 1;
    }
    if (!verify_stream_render_allocations(contract)) {
        std::cerr << "finite first-use/drain/replacement render allocated or broke EOF ownership\n";
        return 1;
    }

    std::cout << "PASS first_use_adoption_and_steady_state_render_do_not_allocate\n";
    std::cout << "PASS failed_publication_preserves_candidate\n";
    std::cout << "PASS finite_first_use_drain_replacement_do_not_allocate\n";
    return 0;
}
