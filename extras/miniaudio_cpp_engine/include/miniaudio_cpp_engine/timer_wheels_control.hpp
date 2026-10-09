#pragma once

#include "plan_exchange.hpp"

#include <timer_wheels/awaitable_timer.h>
#include <timer_wheels/coroutine.h>

#include <chrono>
#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>

namespace miniaudio::cpp_engine {

enum class scheduled_plan_status {
    published,
    cancelled,
    exchange_busy,
    invalid_contract,
    empty,
};

template <class TimerQueue, class Rep, class Period, class Action>
    requires std::invocable<Action&>
[[nodiscard]] auto schedule_control_action(TimerQueue& timers, std::chrono::duration<Rep, Period> delay,
                                           std::stop_token stop, Action action)
    -> timer_wheels::unique_coroutine<timer_wheels::awaitable_timer_status> {
    const auto status = co_await timer_wheels::awaitable_timer{timers, delay, stop};
    if (status == timer_wheels::awaitable_timer_status::expired)
        std::invoke(action);
    co_return status;
}

template <class TimerQueue, class Rep, class Period>
[[nodiscard]] auto schedule_plan_publish(TimerQueue& timers, std::chrono::duration<Rep, Period> delay,
                                         std::stop_token stop, plan_exchange& exchange,
                                         std::unique_ptr<render_plan> candidate)
    -> timer_wheels::unique_coroutine<scheduled_plan_status> {
    const auto timer_status = co_await timer_wheels::awaitable_timer{timers, delay, stop};
    if (timer_status == timer_wheels::awaitable_timer_status::cancelled)
        co_return scheduled_plan_status::cancelled;

    switch (exchange.try_publish(candidate)) {
        case publish_status::published: co_return scheduled_plan_status::published;
        case publish_status::busy: co_return scheduled_plan_status::exchange_busy;
        case publish_status::invalid_contract: co_return scheduled_plan_status::invalid_contract;
        case publish_status::empty: co_return scheduled_plan_status::empty;
    }
    std::terminate();
}

} // namespace miniaudio::cpp_engine
