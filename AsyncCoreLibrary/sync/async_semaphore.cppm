module;

#include <coroutine>
#include <mutex>
#include <queue>
#include <vector>
#include <cstddef>
#include <utility>

export module AsyncCoreLibrary:AsyncSemaphore;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncSemaphore
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_waiters_mtx;
        std::ptrdiff_t m_count{0};
        std::queue<SafeCoroutineHandle> m_waiters;

    public:
        explicit AsyncSemaphore(ICoroScheduler* scheduler = nullptr, std::ptrdiff_t initial_permits = 0) noexcept
            : m_scheduler(scheduler), m_count(initial_permits) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            std::lock_guard lock(m_waiters_mtx);
            m_scheduler = sched;
        }

        bool try_acquire() noexcept {
            std::unique_lock lock(m_waiters_mtx);
            if (m_count > 0) {
                --m_count;
                return true;
            }
            return false;
        }

        struct AcquireAwaiter {
            AsyncSemaphore& m_sem;

            bool await_ready() const noexcept { return false; }

            template <typename Promise>
                requires exec::HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
                std::unique_lock lock(m_sem.m_waiters_mtx);

                if (m_sem.m_count > 0) {
                    --m_sem.m_count;
                    return false;
                }

                m_sem.m_waiters.push(h.promise().get_safe_handle());
                return true;
            }

            void await_resume() const noexcept {}
        };

        [[nodiscard]] AcquireAwaiter acquire_async() noexcept {
            return AcquireAwaiter{*this};
        }

        [[nodiscard]] AcquireAwaiter operator co_await() noexcept {
            return AcquireAwaiter{*this};
        }

        void release(std::ptrdiff_t update = 1) noexcept {
            if (update <= 0) return;

            std::vector<SafeCoroutineHandle> to_resume;
            ICoroScheduler* target_sched{nullptr};

            {
                std::unique_lock lock(m_waiters_mtx);

                while (update > 0) {
                    if (!m_waiters.empty()) {
                        auto safe_h = std::move(m_waiters.front());
                        m_waiters.pop();

                        if (safe_h.is_valid()) {
                            to_resume.push_back(std::move(safe_h));
                            --update;
                        }
                    } else {
                        m_count += update;
                        break;
                    }
                }
                target_sched = m_scheduler;
            }

            for (auto& safe_h : to_resume) {
                if (target_sched) {
                    target_sched->schedule(std::move(safe_h));
                } else {
                    safe_h.try_resume();
                }
            }
        }
    };
}