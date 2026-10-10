#include "miniaudio_cpp_engine/timer_wheels_control.hpp"

#include <timer_wheels/testing/manual_clock.h>
#include <timer_wheels/when_any.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stop_token>
#include <thread>
#include <variant>

namespace {
using namespace std::chrono_literals;
using timer_wheels::awaitable_timer_status;
using clock_type = timer_wheels::testing::manual_clock<struct cpp_engine_timer_clock, std::chrono::microseconds, false>;
using queue_type =
    timer_wheels::testing::manual_heap_timer_queue_st<cpp_engine_timer_clock, std::chrono::microseconds, false>;

struct source_state {
    float value{};
};

struct destruction_state {
    bool destroyed{};
    std::thread::id thread{};
};

class observed_source_node final : public miniaudio::cpp_engine::node {
  public:
    observed_source_node(miniaudio::cpp_engine::channel_layout layout, destruction_state& destruction)
        : layout_(layout), destruction_(destruction) {}
    ~observed_source_node() override {
        destruction_.destroyed = true;
        destruction_.thread = std::this_thread::get_id();
    }

    [[nodiscard]] auto output_layout() const noexcept -> const miniaudio::cpp_engine::channel_layout& override {
        return layout_;
    }
    [[nodiscard]] auto input_count() const noexcept -> std::uint32_t override { return 0; }
    [[nodiscard]] auto accepts_input(std::uint32_t, const miniaudio::cpp_engine::channel_layout&) const noexcept
        -> bool override {
        return false;
    }
    void process(miniaudio::cpp_engine::process_context, std::span<const miniaudio::cpp_engine::const_planar_view>,
                 miniaudio::cpp_engine::planar_view output) noexcept override {
        miniaudio::cpp_engine::clear(output);
    }

  private:
    miniaudio::cpp_engine::channel_layout layout_;
    destruction_state& destruction_;
};

void fill_source(void* opaque, miniaudio::cpp_engine::process_context,
                 miniaudio::cpp_engine::planar_view output) noexcept {
    const auto value = static_cast<source_state*>(opaque)->value;
    std::fill(output.channel(0).begin(), output.channel(0).end(), value);
}

auto require(bool condition, const char* message) -> bool {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

auto expiry_runs_on_the_control_owner() -> bool {
    clock_type::reset();
    queue_type timers;
    bool ran = false;
    auto work = miniaudio::cpp_engine::schedule_control_action(
        timers, 5ms, std::stop_token{}, [&] { ran = true; });
    work.resume();
    if (!require(!work.done() && !ran, "timer completed during registration"))
        return false;

    clock_type::advance(4ms);
    timers.poll();
    if (!require(!work.done() && !ran, "timer completed before its deadline"))
        return false;

    clock_type::advance(1ms);
    timers.poll();
    return require(work.done() && ran && work.result() == awaitable_timer_status::expired,
                   "expired timer did not run its control action");
}

auto cancellation_retires_without_running_the_action() -> bool {
    clock_type::reset();
    queue_type timers;
    std::stop_source stop;
    bool ran = false;
    auto work = miniaudio::cpp_engine::schedule_control_action(
        timers, 1h, stop.get_token(), [&] { ran = true; });
    work.resume();
    if (!require(!work.done() && timers.number_pending() == 1, "cancellable timer did not register"))
        return false;

    stop.request_stop();
    return require(work.done() && !ran && work.result() == awaitable_timer_status::cancelled &&
                       timers.number_pending() == 0,
                   "cancelled timer ran work or retained queue state");
}

auto race_timers(queue_type& timers)
    -> timer_wheels::unique_coroutine<std::variant<awaitable_timer_status, awaitable_timer_status>> {
    std::stop_source siblings;
    co_return co_await timer_wheels::when_any(
        timer_wheels::cancel_remaining, siblings, timer_wheels::awaitable_timer{timers, 1ms},
        timer_wheels::awaitable_timer{timers, 1h});
}

auto fanout_cancels_and_retires_the_loser() -> bool {
    clock_type::reset();
    queue_type timers;
    auto work = race_timers(timers);
    work.resume();
    if (!require(!work.done() && timers.number_pending() == 2, "fanout did not register both timers"))
        return false;

    clock_type::advance(1ms);
    timers.poll();
    if (!require(work.done() && timers.number_pending() == 0, "fanout did not retire the losing timer"))
        return false;
    return require(std::visit([](auto status) { return status == awaitable_timer_status::expired; }, work.result()),
                   "fanout winner was not the expired timer");
}

auto timer_can_publish_a_plan_on_the_control_thread() -> bool {
    using namespace miniaudio::cpp_engine;
    clock_type::reset();
    queue_type timers;
    const stream_contract contract{channel_layout::mono(), 48'000, 8};
    plan_exchange exchange{contract};
    source_state source{0.75f};
    render_plan_builder builder{contract};
    const auto source_node = builder.add(
        std::make_unique<callback_source_node>(contract.layout, &source, fill_source));
    auto plan = std::move(builder).compile(source_node);

    auto work = schedule_plan_publish(timers, 2ms, std::stop_token{}, exchange, std::move(plan));
    work.resume();
    if (!require(!work.done() && !exchange.pending(), "scheduled plan published before its deadline"))
        return false;
    clock_type::advance(2ms);
    timers.poll();
    if (!require(work.done() && work.result() == scheduled_plan_status::published && exchange.pending(),
                 "scheduled plan was not published"))
        return false;

    render_plan* active = nullptr;
    if (!require(exchange.audio_acquire(active) && active != nullptr, "render domain did not acquire the scheduled plan"))
        return false;
    planar_buffer output{contract.layout, contract.quantum};
    active->process({0, contract.quantum, contract.sample_rate}, output.view());
    const auto output_view = output.view();
    for (const auto sample : output_view.channel(0)) {
        if (!require(sample == source.value, "scheduled plan rendered unexpected output"))
            return false;
    }
    exchange.reset_after_stop();
    return true;
}

auto cancelling_plan_publication_retires_on_the_control_thread() -> bool {
    using namespace miniaudio::cpp_engine;
    clock_type::reset();
    queue_type timers;
    std::stop_source stop;
    const stream_contract contract{channel_layout::mono(), 48'000, 8};
    plan_exchange exchange{contract};
    destruction_state destruction;
    const auto control_thread = std::this_thread::get_id();

    {
        render_plan_builder builder{contract};
        const auto source = builder.add(
            std::make_unique<observed_source_node>(contract.layout, destruction));
        auto plan = std::move(builder).compile(source);
        auto work = schedule_plan_publish(timers, 1h, stop.get_token(), exchange, std::move(plan));
        work.resume();
        if (!require(!work.done() && timers.number_pending() == 1, "scheduled publication did not register"))
            return false;

        stop.request_stop();
        if (!require(work.done() && work.result() == scheduled_plan_status::cancelled && !exchange.pending() &&
                         timers.number_pending() == 0 && !destruction.destroyed,
                     "cancelled publication published or destroyed outside owner cleanup"))
            return false;
    }

    return require(destruction.destroyed && destruction.thread == control_thread,
                   "cancelled candidate was not destroyed on the control owner");
}

auto expiry_requests_drain_but_cancellation_does_not() -> bool {
    using namespace miniaudio::cpp_engine;
    clock_type::reset();
    queue_type timers;
    const stream_contract contract{channel_layout::mono(), 48'000, 8};
    source_state source{0.75f};
    plan_exchange exchange{contract};
    render_plan_builder builder{contract};
    const auto output_node = builder.add(std::make_unique<callback_source_node>(contract.layout, &source, fill_source));
    auto plan = std::move(builder).compile_stream(output_node);
    if (exchange.try_publish(plan) != publish_status::published)
        return false;
    render_plan* active = nullptr;
    if (!exchange.audio_acquire(active))
        return false;
    bool action_ran = false;
    bool owner_thread = false;
    drain_request_status requested = drain_request_status::no_active_plan;
    const auto owner = std::this_thread::get_id();
    auto action = [&] {
        action_ran = true;
        owner_thread = std::this_thread::get_id() == owner;
        requested = exchange.request_drain();
    };
    std::stop_source stop;
    auto cancelled = schedule_control_action(timers, 1ms, stop.get_token(), action);
    cancelled.resume();
    stop.request_stop();
    if (!require(cancelled.done() && cancelled.result() == awaitable_timer_status::cancelled && !action_ran &&
                 timers.number_pending() == 0 && !exchange.audio_drain_requested(), "timer cancellation requested a drain"))
        return false;
    auto expiry = schedule_control_action(timers, 2ms, std::stop_token{}, action);
    expiry.resume();
    clock_type::advance(2ms);
    timers.poll();
    if (!require(expiry.done() && expiry.result() == awaitable_timer_status::expired && action_ran && owner_thread &&
                 requested == drain_request_status::queued && exchange.audio_drain_requested(),
                 "timer expiry did not publish an owner-thread drain request"))
        return false;
    planar_buffer output{contract.layout, contract.quantum};
    const auto before = active->process_stream({0, contract.quantum, contract.sample_rate}, output.view());
    if (!require(before.state == stream_state::running && before.valid_frames == contract.quantum,
                 "control request itself executed render-domain draining"))
        return false;
    active->begin_drain();
    const auto after = active->process_stream({contract.quantum, contract.quantum, contract.sample_rate}, output.view());
    return require(after.state == stream_state::complete && after.valid_frames == 0,
                   "render-domain drain did not end the continuous source");
}
} // namespace

int main() {
    const auto expiry = expiry_runs_on_the_control_owner();
    const auto cancellation = cancellation_retires_without_running_the_action();
    const auto fanout = fanout_cancels_and_retires_the_loser();
    const auto scheduled_plan = timer_can_publish_a_plan_on_the_control_thread();
    const auto cancelled_plan = cancelling_plan_publication_retires_on_the_control_thread();
    const auto drain = expiry_requests_drain_but_cancellation_does_not();
    if (expiry)
        std::cout << "PASS expiry_runs_on_the_control_owner\n";
    if (cancellation)
        std::cout << "PASS cancellation_retires_without_running_the_action\n";
    if (fanout)
        std::cout << "PASS fanout_cancels_and_retires_the_loser\n";
    if (scheduled_plan)
        std::cout << "PASS timer_can_publish_a_plan_on_the_control_thread\n";
    if (cancelled_plan)
        std::cout << "PASS cancelling_plan_publication_retires_on_the_control_thread\n";
    if (drain)
        std::cout << "PASS expiry_requests_drain_but_cancellation_does_not\n";
    return expiry && cancellation && fanout && scheduled_plan && cancelled_plan && drain ? 0 : 1;
}
