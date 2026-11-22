module;

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <list>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncLatch;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :WaitState;
import :Concepts;
import :Wake;

namespace async_core::sync {
using common::AwaitableCancellationBase;
using common::HasSafeHandleConcept;
using common::WaitState;
using WaiterQueue = std::queue<std::shared_ptr<WaitState>, std::list<std::shared_ptr<WaitState>>>;
using coro_loop::ICoroScheduler;
using coro_loop::wake;

namespace detail {
inline void resume_waiter(ICoroScheduler* scheduler, const WaitState& state) noexcept {
    if (scheduler != nullptr) {
        wake(*scheduler, state);
    } else {
        (void)state.Handle().TryResume();
    }
}

struct CancelLatchWait {
    std::shared_ptr<WaitState> state;
    ICoroScheduler* scheduler;

    void operator()() noexcept {
        bool resume_needed = false;
        if (state->Complete(WaitState::Cancelled, resume_needed) && resume_needed) {
            resume_waiter(scheduler, *state);
        }
    }
};
} // namespace detail
} // namespace async_core::sync

export namespace async_core::sync {
class AsyncLatch {
    ICoroScheduler* m_scheduler;
    mutable std::mutex m_mtx;
    std::ptrdiff_t m_count;
    WaiterQueue m_waiters;

    /// Completes and (if it had already suspended) resumes every state in `released`. Call WITHOUT
    /// holding m_mtx.
    static void release_all(ICoroScheduler* scheduler, WaiterQueue& released,
                            WaitState::Phase outcome) noexcept {
        // `scheduler` was read by the caller BEFORE the latch could be seen as open: a released
        // waiter may destroy the latch at any moment after that, so nothing of the latch may be
        // touched here.
        while (!released.empty()) {
            bool resume_needed = false;
            if (released.front()->Complete(outcome, resume_needed) && resume_needed) {
                detail::resume_waiter(scheduler, *released.front());
            }
            released.pop();
        }
    }

  public:
    class Awaiter : private AwaitableCancellationBase<detail::CancelLatchWait> {
        friend class AsyncLatch;

        AsyncLatch& m_latch;
        std::shared_ptr<WaitState> m_wait; // stays null when the latch was already open

        explicit Awaiter(AsyncLatch& latch) noexcept
            : m_latch(latch) {}

      public:
        [[nodiscard]] bool await_ready() const noexcept { return m_latch.is_ready(); }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();

            // Everything that can throw is done before the wait is published.
            auto state = std::make_shared<WaitState>(promise.GetSafeHandle());
            m_wait = state;

            // If stop was already requested this runs now and only completes the state, so it is
            // safe before the lock.
            this->watch_stop(this->stop_token_of(promise),
                             detail::CancelLatchWait{state, m_latch.m_scheduler});

            {
                std::lock_guard lock(m_latch.m_mtx);

                if (m_latch.m_count <= 0) {
                    bool unused = false;
                    (void)state->complete(WaitState::Completed,
                                          unused); // loses only if already cancelled
                    return false;                  // continue inline, await_resume() reports it
                }
                if (state->is_finished()) {
                    return false; // cancelled before we got here
                }
                m_latch.m_waiters.push(state);
            }

            return state->arm(); // after this only `state` may be used, `this` may already be gone
        }

        /// true - the latch opened, false - cancelled
        [[nodiscard]] bool await_resume() noexcept {
            this->unwatch_stop();
            return !m_wait || m_wait->GetPhase() == WaitState::Completed;
        }
    };

    explicit AsyncLatch(std::ptrdiff_t count, ICoroScheduler* scheduler = nullptr) noexcept
        : m_scheduler(scheduler)
        , m_count(count < 0 ? 0 : count) {
        assert(count >= 0);
    }

    AsyncLatch(const AsyncLatch&) = delete;
    AsyncLatch& operator=(const AsyncLatch&) = delete;
    AsyncLatch(AsyncLatch&&) = delete;
    AsyncLatch& operator=(AsyncLatch&&) = delete;

    ~AsyncLatch() {
        WaiterQueue pending;
        {
            std::lock_guard lock(m_mtx);
            pending.swap(m_waiters);
        }
        release_all(m_scheduler, pending,
                    WaitState::Cancelled); // unwind instead of hanging forever
    }

    void count_down(std::ptrdiff_t n = 1) noexcept {
        if (n <= 0) {
            return;
        }
        ICoroScheduler* const scheduler = m_scheduler; // before the latch can be seen as open

        WaiterQueue released;
        {
            std::lock_guard lock(m_mtx);
            const std::ptrdiff_t current = m_count;
            assert(n <= current && "AsyncLatch::count_down below zero");
            if (current <= 0) {
                return;
            }
            const std::ptrdiff_t next = (n >= current) ? 0 : current - n;
            m_count = next;
            if (next == 0) {
                released.swap(m_waiters);
            }
        }
        // complete first, resume only if the coroutine had already suspended; outside the lock
        release_all(scheduler, released, WaitState::Completed);
    }

    [[nodiscard]] bool is_ready() const noexcept {
        // Under the mutex on purpose: when this sees "open", the final count_down() has finished
        // with the mutex, so the caller may destroy the latch right away (barrier style).
        std::lock_guard lock(m_mtx);
        return m_count <= 0;
    }

    [[nodiscard]] bool try_wait() const noexcept { return is_ready(); }

    [[nodiscard]] Awaiter wait() noexcept { return Awaiter{*this}; }

    [[nodiscard]] Awaiter operator co_await() noexcept { return Awaiter{*this}; }
};
} // namespace async_core::sync