module;

#include <coroutine>
#include <memory>
#include <mutex>
#include <list>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :WaitState;
import :Concepts;
import :CancelWait;
import :Wake;

namespace async_core::sync {
using common::AwaitableCancellationBase;
using common::HasSafeHandleConcept;
using common::WaitState;
using WaiterQueue =
    std::queue<std::shared_ptr<WaitState>,
               std::list<std::shared_ptr<WaitState>>>; 
using coro_loop::CancelWait;
using coro_loop::ICoroScheduler;
using coro_loop::wake;
} // namespace async_core::sync

export namespace async_core::sync {
enum class EventResetMode : std::uint8_t {
    Auto,  
    Manual 
};

template <EventResetMode Mode> class AsyncEvent {
    static constexpr bool kAutoReset = (Mode == EventResetMode::Auto);

    ICoroScheduler& m_scheduler;
    std::mutex m_mtx;
    WaiterQueue m_waiters;  
    bool m_signaled{false}; 

    /// Caller holds m_mtx.
    void purge_dead_front() noexcept {
        while (!m_waiters.empty() && m_waiters.front()->IsFinished()) {
            m_waiters.pop();
        }
    }

    /// Completes `state` with `outcome`. The coroutine is resumed only if it had already suspended
    /// and only by the call that completed the wait, so it is never resumed twice. Returns whether
    /// THIS call completed it.
    bool finish(WaitState& state, WaitState::Phase outcome) noexcept {
        bool resume_needed = false;
        if (!state.Complete(outcome, resume_needed)) {
            return false;
        }
        if (resume_needed) {
            wake(m_scheduler, state);
        }
        return true;
    }

  public:
    class Awaiter : private AwaitableCancellationBase<CancelWait> {
        friend class AsyncEvent;

        AsyncEvent& m_event;
        std::shared_ptr<WaitState> m_wait; // stays null when the wait completed in await_ready()

        explicit Awaiter(AsyncEvent& event) noexcept
            : m_event(event) {}

      public:
        [[nodiscard]] bool await_ready() {
            std::lock_guard lock(m_event.m_mtx);
            if (m_event.m_signaled) {
                if constexpr (kAutoReset) {
                    m_event.m_signaled = false;
                }
                return true;
            }
            return false;
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();

            // Everything that can throw happens before the wait is published. Not noexcept on
            // purpose: running out of memory here is reported to the awaiting coroutine, it does
            // not terminate the process.
            auto state = std::make_shared<WaitState>(promise.GetSafeHandle());
            m_wait = state;

            // If stop was already requested this runs right now; it only completes the state, so it
            // is safe before the lock.
            this->watch_stop(this->stop_token_of(promise), CancelWait{state, &m_event.m_scheduler});

            {
                std::lock_guard lock(m_event.m_mtx);

                if (m_event.m_signaled) {
                    bool unused = false;
                    const bool completed = state->complete(WaitState::Completed, unused);
                    if constexpr (kAutoReset) {
                        if (completed) {
                            m_event.m_signaled = false; // took the signal
                        } // else: cancelled in the meantime, the signal stays for someone else
                    } else {
                        (void)completed; // a manual-reset signal is not consumed
                    }
                    return false; // continue inline, await_resume() reports the outcome
                }
                if (state->is_finished()) {
                    return false; // cancelled before we got here
                }

                m_event.purge_dead_front();
                m_event.m_waiters.push(state);
            }

            return state->arm(); // after this only `state` may be used
        }

        /// true - signaled, false - cancelled
        [[nodiscard]] bool await_resume() noexcept {
            this->unwatch_stop();
            return !m_wait || m_wait->GetPhase() == WaitState::Completed;
        }
    };

    explicit AsyncEvent(ICoroScheduler& scheduler, bool initially_signaled = false) noexcept
        : m_scheduler(scheduler)
        , m_signaled(initially_signaled) {}

    AsyncEvent(const AsyncEvent&) = delete;
    AsyncEvent& operator=(const AsyncEvent&) = delete;
    AsyncEvent(AsyncEvent&&) = delete;
    AsyncEvent& operator=(AsyncEvent&&) = delete;

    /// Releases parked waiters as cancelled so they unwind instead of hanging forever.
    ~AsyncEvent() {
        WaiterQueue pending;
        {
            std::lock_guard lock(m_mtx);
            pending.swap(m_waiters);
        }
        while (!pending.empty()) {
            (void)finish(*pending.front(), WaitState::Cancelled);
            pending.pop();
        }
    }

    void set() noexcept {
        ICoroScheduler& scheduler =
            m_scheduler; // read before any waiter can proceed: it may destroy the event
        if constexpr (kAutoReset) {
            // wake ONE live waiter, or remember one signal
            std::shared_ptr<WaitState> chosen;
            bool resume_needed = false;
            {
                std::lock_guard lock(m_mtx);
                while (!m_waiters.empty()) {
                    auto candidate = std::move(m_waiters.front());
                    m_waiters.pop();
                    bool needed = false;
                    if (candidate->Complete(WaitState::Completed,
                                            needed)) { // cancelled nodes fail here and are dropped
                        chosen = std::move(candidate);
                        resume_needed = needed;
                        break;
                    }
                }
                if (!chosen) {
                    m_signaled = true;
                    return;
                }
            }
            if (resume_needed) {
                wake(scheduler, *chosen);
            } // else: the waiter has not finished suspending yet; its arm() fails and it continues
              // inline
        } else {
            // become signaled and wake EVERY waiter that is still waiting
            WaiterQueue released;
            {
                std::lock_guard lock(m_mtx);
                if (m_signaled) {
                    return;
                }
                m_signaled = true;
                released.swap(m_waiters);
            }
            while (!released.empty()) {
                bool resume_needed = false; // complete first, resume only if it had suspended
                if (released.front()->Complete(WaitState::Completed, resume_needed) &&
                    resume_needed) {
                    wake(scheduler, *released.front());
                }
                released.pop();
            }
        }
    }

    void reset() noexcept
        requires(!kAutoReset)
    {
        std::lock_guard lock(m_mtx);
        m_signaled = false;
    }

    [[nodiscard]] Awaiter operator co_await() noexcept { return Awaiter{*this}; }
};

using AsyncAutoResetEvent = AsyncEvent<EventResetMode::Auto>;
using AsyncManualResetEvent = AsyncEvent<EventResetMode::Manual>;
} // namespace async_core::sync