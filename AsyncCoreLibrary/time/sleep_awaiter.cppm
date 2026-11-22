module;

#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>
#include <utility>

export module AsyncCoreLibrary:SleepAwaiter;

import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :WaitState;
import :Concepts;
import :Wake;
import :TimerQueue;
import :ICoroScheduler;

namespace async_core::time {
using common::AwaitableCancellationBase;
using common::HasSafeHandleConcept;
using common::WaitState;
using coro_loop::ICoroScheduler;
using coro_loop::wake;

namespace detail {
struct CancelSleep {
    std::shared_ptr<WaitState> state;
    TimerQueue* queue;
    TimerId id;

    void operator()() noexcept {
        bool resume_needed = false;
        if (state->Complete(WaitState::Cancelled, resume_needed)) {
            (void)queue->CancelTimer(id);
            if (resume_needed) {
                wake(queue->scheduler(), *state);
            }
        }
    }
};

/// now + d, clamped so that absurd durations cannot overflow the clock.
inline Clock::time_point deadline_after(const Clock::duration d) noexcept {
    constexpr auto kLongest = std::chrono::hours(24 * 365 * 100);
    const auto now = Clock::now();
    if (d <= Clock::duration::zero()) {
        return now;
    }
    return now + (d > kLongest ? std::chrono::duration_cast<Clock::duration>(kLongest) : d);
}
} // namespace detail
} // namespace async_core::time

export namespace async_core::time {
class SleepAwaiter : private AwaitableCancellationBase<detail::CancelSleep> {
    TimerQueue& m_queue;
    Clock::time_point m_deadline;
    std::shared_ptr<WaitState> m_wait; // null if no sleep was needed

  protected:
    SleepAwaiter(TimerQueue& queue, const Clock::time_point deadline) noexcept
        : m_queue(queue)
        , m_deadline(deadline) {}

  public:
    [[nodiscard]] bool await_ready() const noexcept { return Clock::now() >= m_deadline; }

    template <typename PromiseT>
        requires HasSafeHandleConcept<PromiseT>
    bool await_suspend(std::coroutine_handle<PromiseT> caller) {
        auto& promise = caller.promise();

        auto state = std::make_shared<WaitState>(promise.GetSafeHandle());
        m_wait = state;

        ICoroScheduler& scheduler = m_queue.scheduler();
        const auto id =
            m_queue.AddTimer(m_deadline, [state, &scheduler](const TimerOutcome outcome) {
                bool resume_needed = false;
                const auto result =
                    (outcome == TimerOutcome::Fired) ? WaitState::Completed : WaitState::Cancelled;
                if (state->complete(result, resume_needed) && resume_needed) {
                    wake(scheduler, *state);
                }
            });

        if (!id) {
            bool unused = false;
            (void)state->complete(WaitState::Cancelled,
                                  unused); // queue is shutting down: do not sleep
            return false;
        }

        this->watch_stop(this->stop_token_of(promise), detail::CancelSleep{state, &m_queue, *id});

        return state
            ->arm(); // false: the timer or a stop already finished the wait, continue inline
    }

    [[nodiscard]] bool await_resume() noexcept {
        this->unwatch_stop();
        return !m_wait || m_wait->GetPhase() == WaitState::Completed;
    }
};
} // namespace async_core::time