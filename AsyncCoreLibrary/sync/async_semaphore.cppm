module;

#include <mutex>
#include <memory>
#include <queue>
#include <coroutine>

export module AsyncCoreLibrary:AsyncSemaphore;

import :ICoroScheduler;

using async_core::coro_loop::ICoroScheduler;

export namespace async_core::sync
{
        class AsyncSemaphore {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_waiters_mtx;
        ptrdiff_t m_count{0};
        std::queue<std::coroutine_handle<>> m_waiters;

    public:
        explicit AsyncSemaphore(ICoroScheduler* scheduler, ptrdiff_t initial_permits) noexcept
            : m_scheduler(scheduler), m_count(initial_permits) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
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

            bool await_ready() noexcept {
                return m_sem.try_acquire();
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept {
                std::unique_lock lock(m_sem.m_waiters_mtx);

                if (m_sem.m_count > 0) {
                    --m_sem.m_count;
                    return false;
                }

                m_sem.m_waiters.push(h);
                return true;
            }

            void await_resume() noexcept {}
        };

        [[nodiscard]] AcquireAwaiter acquire_async() noexcept {
            return AcquireAwaiter{*this};
        }

        [[nodiscard]] auto operator co_await() noexcept {
            return AcquireAwaiter{*this};
        }

        void release(ptrdiff_t update = 1) noexcept {
            if (update <= 0) return;

            std::vector<std::coroutine_handle<>> to_resume;
            {
                std::unique_lock lock(m_waiters_mtx);

                while (update > 0) {
                    if (!m_waiters.empty()) {
                        to_resume.push_back(m_waiters.front());
                        m_waiters.pop();
                    } else {
                        m_count += update;
                        break;
                    }
                    --update;
                }
            }

            if (m_scheduler) {
                for (auto h : to_resume) {
                    m_scheduler->schedule(h);
                }
            }
        }
    };
}
