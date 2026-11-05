module;

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

export module AsyncCoreLibrary:AsyncSemaphore;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :ScheduleTypes;

using async_core::coro_loop::ICoroScheduler;
using async_core::common::ScheduleStatus;
using async_core::common::SafeCoroutineHandle;
using async_core::common::HasSafeHandle;

export namespace async_core::sync
{
    class AsyncSemaphore
    {
        ICoroScheduler& m_scheduler;
        std::mutex m_waiters_mtx;
        std::ptrdiff_t m_count{0};
        std::queue<SafeCoroutineHandle> m_waiters;

    public:
        explicit AsyncSemaphore(ICoroScheduler& scheduler, std::ptrdiff_t initial_permits = 0) noexcept
            : m_scheduler(scheduler), m_count(initial_permits) {}

        AsyncSemaphore(const AsyncSemaphore&) = delete;
        AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;
        AsyncSemaphore(AsyncSemaphore&&) noexcept = delete;
        AsyncSemaphore& operator=(AsyncSemaphore&&) noexcept = delete;

        bool try_acquire() noexcept {
            std::unique_lock lock(m_waiters_mtx);
            if (m_count > 0 && m_waiters.empty()) {
                --m_count;
                return true;
            }
            return false;
        }

        struct AcquireAwaiter {
            AsyncSemaphore& m_sem;

            bool await_ready() const noexcept {
                return m_sem.try_acquire();
            }

            template <typename Promise>
                requires HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
                std::unique_lock lock(m_sem.m_waiters_mtx);

                if (m_sem.m_count > 0 && m_sem.m_waiters.empty()) {
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

            while (update > 0) {
                SafeCoroutineHandle handle_to_resume{};
                {
                    std::unique_lock lock(m_waiters_mtx);

                    while (!m_waiters.empty()) {
                        handle_to_resume = std::move(m_waiters.front());
                        m_waiters.pop();

                        if (handle_to_resume.is_valid()) {
                            break;
                        }
                    }

                    if (!handle_to_resume.is_valid()) {
                        m_count += update;
                        return;
                    }
                }

                const ScheduleStatus status = m_scheduler.schedule(std::move(handle_to_resume));

                if (status == ScheduleStatus::Success) {
                    --update;
                    continue;
                }

                if (status == ScheduleStatus::InvalidHandle) {
                    continue;
                }

                if (status == ScheduleStatus::SchedulerStopped) {
                    std::unique_lock lock(m_waiters_mtx);
                    m_count += update;
                    return;
                }

                std::unique_lock lock(m_waiters_mtx);
                m_count += update;
                return;
            }
        }
    };
}