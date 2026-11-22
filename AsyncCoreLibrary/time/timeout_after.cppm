module;

#include <atomic>
#include <chrono>
#include <memory>
#include <type_traits>
#include <utility>

export module AsyncCoreLibrary:TimeoutAfter;

import :TimerQueue;
import :SafeCoroutineHandle;
import :Exceptions;
import :Task;

namespace async_core::time
{
    using common::OperationCancelledException;
    using common::SafeCoroutineHandle;
    using common::TimeoutException;
    using exec::Task;

    namespace detail
    {
        struct TimerGuard
        {
            TimerQueue& queue;
            TimerId id;

            TimerGuard(TimerQueue& q, const TimerId timer_id) noexcept : queue(q), id(timer_id) {}
            TimerGuard(const TimerGuard&) = delete;
            TimerGuard& operator=(const TimerGuard&) = delete;
            ~TimerGuard() { (void)queue.CancelTimer(id); }
        };
    }
}

export namespace async_core::time
{

    template <typename TaskT, typename Rep, typename Period>
        requires (!std::is_lvalue_reference_v<TaskT>) && requires { typename std::remove_cvref_t<TaskT>::value_type; }
    [[nodiscard]] auto timeout_after(TimerQueue& timers, TaskT task, const std::chrono::duration<Rep, Period> timeout)
        -> Task<typename std::remove_cvref_t<TaskT>::value_type>
    {
        using Value = typename std::remove_cvref_t<TaskT>::value_type;

        auto timed_out = std::make_shared<std::atomic<bool>>(false);
        SafeCoroutineHandle child = task.GetSafeHandle();

        // The callback runs on the timer thread and only flips a flag and requests stop; it owns copies of everything it
        // needs, so it is harmless if it runs a moment after this coroutine has finished.
        const auto id = timers.AddTimer(timeout, [timed_out, child](const TimerOutcome outcome) mutable {
            if (outcome == TimerOutcome::Fired) {
                timed_out->store(true, std::memory_order_release);
                child.RequestStop();
            }
        });
        if (!id) {
            throw OperationCancelledException{};
        }
        const detail::TimerGuard guard{timers, *id};

        try {
            if constexpr (std::is_void_v<Value>) {
                co_await std::move(task);
                if (timed_out->load(std::memory_order_acquire)) {
                    throw TimeoutException{};       
                }
            } else {
                auto result = co_await std::move(task);
                if (timed_out->load(std::memory_order_acquire)) {
                    throw TimeoutException{};       
                }
                co_return std::move(result);
            }
        } catch (const OperationCancelledException&) {
            if (timed_out->load(std::memory_order_acquire)) {
                throw TimeoutException{};
            }
            throw;
        }
    }
}