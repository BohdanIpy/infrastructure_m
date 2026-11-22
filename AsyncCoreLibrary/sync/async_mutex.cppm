module;

#include <cassert>
#include <coroutine>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncMutex;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :WaitState;
import :Concepts;
import :CancelWait;
import :Wake;
import :Exceptions;

namespace async_core::sync {
using common::AwaitableCancellationBase;
using common::HasSafeHandleConcept;
using common::OperationCancelledException;
using common::WaitState;
using coro_loop::CancelWait;
using coro_loop::ICoroScheduler;
using coro_loop::wake;
using WaiterQueue = std::queue<std::shared_ptr<WaitState>, std::list<std::shared_ptr<WaitState>>>;
} // namespace async_core::sync

export namespace async_core::sync {
/**
 * Mutex for coroutines. Waiting does not block a thread.
 *
 *   auto guard = co_await mutex;       // or mutex.lock_async()
 *
 * - FIFO and direct hand-off: unlock() gives the lock to the first waiting coroutine without ever
 * releasing it, so nobody can sneak in between (try_lock() also fails while somebody waits).
 * - Cancellation: if the awaiting coroutine is cancelled while waiting, `co_await mutex` throws
 *   OperationCancelledException and the lock is NOT held. A cancelled waiter never receives the
 * lock.
 * - A waiter is resumed through the scheduler; if the scheduler refuses the handle it is resumed
 * inline instead, so a hand-off is never lost (the old code dropped the waiter, which then hung
 * forever).
 * - The lock is not tied to a thread: a coroutine may be resumed on another worker while holding
 * it.
 *
 * Do not co_await the same mutex twice in one coroutine (self-deadlock), as with any non-recursive
 * mutex. Parked waiters are released as cancelled by the destructor. The scheduler must outlive the
 * mutex.
 */
class AsyncMutex {
    ICoroScheduler& m_scheduler;
    std::mutex m_mtx;
    bool m_locked{false};  // guarded by m_mtx
    WaiterQueue m_waiters; // guarded by m_mtx

    /// Caller holds m_mtx.
    void purge_dead_front() noexcept {
        while (!m_waiters.empty() && m_waiters.front()->IsFinished()) {
            m_waiters.pop();
        }
    }

  public:
    class [[nodiscard]] ScopedLock {
        AsyncMutex* m_mutex{nullptr};

      public:
        ScopedLock() noexcept = default;
        explicit ScopedLock(AsyncMutex* mutex) noexcept
            : m_mutex(mutex) {} // adopts a lock that is already held

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

        ScopedLock(ScopedLock&& other) noexcept
            : m_mutex(std::exchange(other.m_mutex, nullptr)) {}

        ScopedLock& operator=(ScopedLock&& other) noexcept {
            if (this != &other) {
                unlock();
                m_mutex = std::exchange(other.m_mutex, nullptr);
            }
            return *this;
        }

        ~ScopedLock() { unlock(); }

        /// Unlocks early. Safe to call twice.
        void unlock() noexcept {
            if (AsyncMutex* mutex = std::exchange(m_mutex, nullptr)) {
                mutex->unlock();
            }
        }

        /// Gives up the guard WITHOUT unlocking; the caller becomes responsible for
        /// AsyncMutex::unlock().
        AsyncMutex* release() noexcept { return std::exchange(m_mutex, nullptr); }

        [[nodiscard]] bool owns_lock() const noexcept { return m_mutex != nullptr; }
    };

    class LockAwaiter : private AwaitableCancellationBase<CancelWait> {
        friend class AsyncMutex;

        AsyncMutex& m_mutex;
        std::shared_ptr<WaitState> m_wait; // null if the lock was taken in await_ready()

        explicit LockAwaiter(AsyncMutex& mutex) noexcept
            : m_mutex(mutex) {}

      public:
        [[nodiscard]] bool await_ready() noexcept { return m_mutex.try_lock(); }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();

            auto state = std::make_shared<WaitState>(promise.GetSafeHandle());
            m_wait = state;

            // Only completes the state, so it is safe before the lock (even if stop was already
            // requested).
            this->watch_stop(this->stop_token_of(promise), CancelWait{state, &m_mutex.m_scheduler});

            {
                std::lock_guard lock(m_mutex.m_mtx);
                m_mutex.purge_dead_front();

                if (!m_mutex.m_locked && m_mutex.m_waiters.empty()) {
                    bool unused = false;
                    if (state->complete(WaitState::Completed, unused)) {
                        m_mutex.m_locked = true; // we own it; continue inline
                    } // else cancelled meanwhile: lock stays free, await_resume throws
                    return false;
                }
                if (state->is_finished()) {
                    return false; // cancelled before queueing
                }
                m_mutex.m_waiters.push(state);
            }

            return state->arm(); // after this only `state` may be used
        }

        [[nodiscard]] ScopedLock await_resume() {
            this->unwatch_stop();
            if (m_wait && m_wait->GetPhase() != WaitState::Completed) {
                throw OperationCancelledException{}; // cancelled: the lock was never handed to us
            }
            return ScopedLock{&m_mutex};
        }
    };

    explicit AsyncMutex(ICoroScheduler& scheduler) noexcept
        : m_scheduler(scheduler) {}

    AsyncMutex(const AsyncMutex&) = delete;
    AsyncMutex& operator=(const AsyncMutex&) = delete;
    AsyncMutex(AsyncMutex&&) = delete;
    AsyncMutex& operator=(AsyncMutex&&) = delete;

    ~AsyncMutex() {
        WaiterQueue pending;
        {
            std::lock_guard lock(m_mtx);
            pending.swap(m_waiters);
        }
        while (!pending.empty()) {
            bool resume_needed = false;
            if (pending.front()->Complete(WaitState::Cancelled, resume_needed) && resume_needed) {
                wake(m_scheduler, *pending.front());
            }
            pending.pop();
        }
    }

    /// Takes the lock if it is free and nobody is queued. After true the caller must call unlock()
    /// (or adopt it with ScopedLock{&m}).
    [[nodiscard]] bool try_lock() noexcept {
        std::lock_guard lock(m_mtx);
        purge_dead_front();
        if (m_locked || !m_waiters.empty()) {
            return false;
        }
        m_locked = true;
        return true;
    }

    [[nodiscard]] LockAwaiter lock_async() noexcept { return LockAwaiter{*this}; }
    [[nodiscard]] LockAwaiter operator co_await() noexcept { return LockAwaiter{*this}; }

    /// Hands the lock to the first live waiter, or frees it.
    void unlock() noexcept {
        ICoroScheduler& scheduler =
            m_scheduler; // the new owner may destroy the mutex as soon as it runs
        std::shared_ptr<WaitState> chosen;
        bool resume_needed = false;
        {
            std::lock_guard lock(m_mtx);
            assert(m_locked && "double unlock of AsyncMutex");

            while (!m_waiters.empty()) {
                auto candidate = std::move(m_waiters.front());
                m_waiters.pop();
                bool needed = false;
                if (candidate->Complete(WaitState::Completed,
                                        needed)) { // cancelled waiters fail here and are skipped
                    chosen = std::move(candidate);
                    resume_needed = needed;
                    break;
                }
            }
            if (!chosen) {
                m_locked = false;
            } // else: the lock stays locked, it now belongs to `chosen`
        }
        if (chosen && resume_needed) {
            wake(scheduler, *chosen); // else it has not finished suspending: its arm() fails and it
                                      // continues inline
        }
    }
};
} // namespace async_core::sync