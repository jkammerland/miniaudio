#pragma once

#include "render_plan.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace miniaudio::cpp_engine {

static_assert(std::atomic<void*>::is_always_lock_free,
              "the render-plan exchange requires lock-free pointer atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "the render-plan exchange requires lock-free 64-bit atomics");

enum class publish_status {
    published,
    busy,
    invalid_contract,
    empty,
};

enum class drain_request_status { queued, no_active_plan, unsupported_plan };

class plan_exchange {
  public:
    explicit plan_exchange(stream_contract contract) : contract_(contract) {
        if (!contract.valid())
            throw std::invalid_argument("a plan exchange requires a valid stream contract");
    }

    plan_exchange(const plan_exchange&) = delete;
    auto operator=(const plan_exchange&) -> plan_exchange& = delete;

    [[nodiscard]] auto contract() const noexcept -> const stream_contract& { return contract_; }

    // Control-domain operation. The candidate is consumed only on success.
    auto try_publish(std::unique_ptr<render_plan>& candidate) -> publish_status {
        if (!candidate)
            return publish_status::empty;
        if (candidate->contract() != contract_)
            return publish_status::invalid_contract;
        if (pending_.load(std::memory_order_acquire) != nullptr)
            return publish_status::busy;

        // Complete every potentially throwing allocation before consuming the
        // caller's plan or advancing the generation.
        retained_.reserve(retained_.size() + 1);
        auto retained = std::make_unique<retained_plan>();
        retained->generation = next_generation_;
        auto* published = retained.get();
        retained_.push_back(std::move(retained));
        published->plan = std::move(candidate);
        ++next_generation_;
        pending_.store(published, std::memory_order_release);
        return publish_status::published;
    }

    // Render-domain operation. Call only immediately before rendering a quantum.
    auto audio_acquire(render_plan*& active) noexcept -> bool {
        auto* update = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (update == nullptr)
            return false;

        active = update->plan.get();
        audio_generation_ = update->generation;
        acknowledged_.store(audio_generation_, std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto acknowledged_generation() const noexcept -> std::uint64_t {
        return acknowledged_.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto pending() const noexcept -> bool {
        return pending_.load(std::memory_order_acquire) != nullptr;
    }

    // Control-domain operation. A request targets the observed active generation,
    // never a pending or subsequently adopted replacement.
    [[nodiscard]] auto request_drain() noexcept -> drain_request_status {
        const auto generation = acknowledged_.load(std::memory_order_acquire);
        if (generation == 0)
            return drain_request_status::no_active_plan;
        for (const auto& retained : retained_) {
            if (retained->generation != generation)
                continue;
            if (!retained->plan->stream_processing())
                return drain_request_status::unsupported_plan;
            drain_requested_.store(generation, std::memory_order_release);
            return drain_request_status::queued;
        }
        return drain_request_status::no_active_plan;
    }

    // Render-domain operations; audio_generation_ is not control-thread state.
    [[nodiscard]] auto audio_generation() const noexcept -> std::uint64_t { return audio_generation_; }
    [[nodiscard]] auto audio_drain_requested() const noexcept -> bool {
        return audio_generation_ != 0 && drain_requested_.load(std::memory_order_acquire) == audio_generation_;
    }

    // Control-domain operation. The current generation remains retained.
    auto reclaim_acknowledged() -> std::size_t {
        const auto acknowledged = acknowledged_.load(std::memory_order_acquire);
        const auto before = retained_.size();
        std::erase_if(retained_, [acknowledged](const auto& retained) {
            return retained->generation < acknowledged;
        });
        return before - retained_.size();
    }

    // Control-domain operation after the render domain has stopped and joined.
    void reset_after_stop() noexcept {
        pending_.store(nullptr, std::memory_order_relaxed);
        retained_.clear();
        acknowledged_.store(0, std::memory_order_relaxed);
        drain_requested_.store(0, std::memory_order_relaxed);
        audio_generation_ = 0;
    }

  private:
    struct retained_plan {
        std::uint64_t generation{};
        std::unique_ptr<render_plan> plan;
    };

    stream_contract contract_;
    std::atomic<retained_plan*> pending_{};
    std::atomic<std::uint64_t> acknowledged_{};
    std::atomic<std::uint64_t> drain_requested_{};
    std::uint64_t next_generation_{1};
    std::uint64_t audio_generation_{};
    std::vector<std::unique_ptr<retained_plan>> retained_;
};

} // namespace miniaudio::cpp_engine
