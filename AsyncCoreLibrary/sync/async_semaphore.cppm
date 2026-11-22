module;

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncSemaphore;

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
 * Counting semaphore for coroutines.
 *
 *   co_await sem;          // takes one permit, waits (without blocking a thread) if there is none
 *   sem.release(n);        // gives n permits: waiters first (FIFO), the rest is added to the
 * counter
 *
 * - Cancellation: a cancelled waiter does NOT get a permit and `co_await sem` throws
 * OperationCancelledException. A permit is never lost or duplicated: it goes either to the waiter
 * (then its cancellation is too late) or back to the pool.
 * - A waiter is resumed through the scheduler; if the scheduler refuses the handle it is resumed
 * inline, so permits are never lost on a stopping scheduler (the old code returned them to the
 * counter but lost the dropped waiter).
 * - try_acquire() fails while somebody is queued (no queue jumping).
 * Parked waiters are released as cancelled by the destructor. The scheduler must outlive the
 * semaphore.
 */
class AsyncSemaphore {
    ICoroScheduler& m_scheduler;
    std::mutex m_mtx;
    std::ptrdiff_t m_count; // guarded by m_mtx
    WaiterQueue m_waiters;  // guarded by m_mtx

    void purge_dead_front() noexcept {
        while (!m_waiters.empty() && m_waiters.front()->IsFinished()) {
            m_waiters.pop();
        }
    }

  public:
    class AcquireAwaiter : private AwaitableCancellationBase<CancelWait> {
        friend class AsyncSemaphore;

        AsyncSemaphore& m_sem;
        std::shared_ptr<WaitState> m_wait;

        explicit AcquireAwaiter(AsyncSemaphore& sem) noexcept
            : m_sem(sem) {}

      public:
        [[nodiscard]] bool await_ready() noexcept { return m_sem.try_acquire(); }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();

            auto state = std::make_shared<WaitState>(promise.GetSafeHandle());
            m_wait = state;

            this->watch_stop(this->stop_token_of(promise), CancelWait{state, &m_sem.m_scheduler});

            {
                std::lock_guard lock(m_sem.m_mtx);
                m_sem.purge_dead_front();

                if (m_sem.m_count > 0 && m_sem.m_waiters.empty()) {
                    bool unused = false;
                    if (state->complete(WaitState::Completed, unused)) {
                        --m_sem.m_count; // took a permit; continue inline
                    } // else cancelled meanwhile: the permit stays in the pool
                    return false;
                }
                if (state->is_finished()) {
                    return false;
                }
                m_sem.m_waiters.push(state);
            }

            return state->arm();
        }

        void await_resume() {
            this->unwatch_stop();
            if (m_wait && m_wait->GetPhase() != WaitState::Completed) {
                throw OperationCancelledException{};
            }
        }
    };

    explicit AsyncSemaphore(ICoroScheduler& scheduler, std::ptrdiff_t initial_permits = 0) noexcept
        : m_scheduler(scheduler)
        , m_count(initial_permits < 0 ? 0 : initial_permits) {
        assert(initial_permits >= 0);
    }

    AsyncSemaphore(const AsyncSemaphore&) = delete;
    AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;
    AsyncSemaphore(AsyncSemaphore&&) = delete;
    AsyncSemaphore& operator=(AsyncSemaphore&&) = delete;

    ~AsyncSemaphore() {
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

    [[nodiscard]] bool try_acquire() noexcept {
        std::lock_guard lock(m_mtx);
        purge_dead_front();
        if (m_count > 0 && m_waiters.empty()) {
            --m_count;
            return true;
        }
        return false;
    }

    [[nodiscard]] AcquireAwaiter acquire_async() noexcept { return AcquireAwaiter{*this}; }
    [[nodiscard]] AcquireAwaiter operator co_await() noexcept { return AcquireAwaiter{*this}; }

    void release(std::ptrdiff_t update = 1) noexcept {
        ICoroScheduler& scheduler = m_scheduler;
        while (update > 0) {
            std::shared_ptr<WaitState> chosen;
            bool resume_needed = false;
            {
                std::lock_guard lock(m_mtx);
                while (!m_waiters.empty()) {
                    auto candidate = std::move(m_waiters.front());
                    m_waiters.pop();
                    bool needed = false;
                    if (candidate->Complete(WaitState::Completed,
                                            needed)) { // cancelled waiters are skipped
                        chosen = std::move(candidate);
                        resume_needed = needed;
                        break;
                    }
                }
                if (!chosen) {
                    m_count += update; // nobody is waiting: the rest goes to the pool
                    return;
                }
            }
            --update;
            if (resume_needed) {
                wake(scheduler, *chosen);
            }
        }
    }
};
} // namespace async_core::sync