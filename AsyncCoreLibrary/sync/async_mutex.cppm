module;

#include <coroutine>
#include <mutex>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncMutex;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncMutex
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_mtx;
        bool m_locked{false};
        std::queue<SafeCoroutineHandle> m_waiters;

    public:
        explicit AsyncMutex(ICoroScheduler* scheduler = nullptr) noexcept
            : m_scheduler(scheduler) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            std::lock_guard lock(m_mtx);
            m_scheduler = sched;
        }

        class [[nodiscard]] ScopedLock {
            AsyncMutex* m_mutex{nullptr};

        public:
            explicit ScopedLock(AsyncMutex* mtx) noexcept : m_mutex(mtx) {}

            ~ScopedLock() {
                if (m_mutex) {
                    m_mutex->unlock();
                }
            }

            ScopedLock(const ScopedLock&) = delete;
            ScopedLock& operator=(const ScopedLock&) = delete;

            ScopedLock(ScopedLock&& other) noexcept
                : m_mutex(std::exchange(other.m_mutex, nullptr)) {}

            ScopedLock& operator=(ScopedLock&& other) noexcept {
                if (this != &other) {
                    if (m_mutex) {
                        m_mutex->unlock();
                    }
                    m_mutex = std::exchange(other.m_mutex, nullptr);
                }
                return *this;
            }

            void release() noexcept {
                m_mutex = nullptr;
            }
        };

        struct LockAwaiter
        {
            AsyncMutex& m_mutex;

            bool await_ready() noexcept { return false; }

            template <typename Promise>
                requires exec::HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
                std::unique_lock lock(m_mutex.m_mtx);

                if (!m_mutex.m_locked) {
                    m_mutex.m_locked = true;
                    return false;
                }

                m_mutex.m_waiters.push(h.promise().get_safe_handle());
                return true;
            }

            ScopedLock await_resume() const noexcept {
                return ScopedLock(&m_mutex);
            }
        };

        bool try_lock() noexcept {
            std::unique_lock lock(m_mtx);
            if (m_locked) return false;
            m_locked = true;
            return true;
        }

        [[nodiscard]] LockAwaiter lock_async() noexcept {
            return LockAwaiter{*this};
        }

        [[nodiscard]] LockAwaiter operator co_await() noexcept {
            return LockAwaiter{*this};
        }

        void unlock() noexcept {
            SafeCoroutineHandle handle_to_resume;
            ICoroScheduler* target_sched{nullptr};

            {
                std::unique_lock lock(m_mtx);

                while (!m_waiters.empty()) {
                    handle_to_resume = std::move(m_waiters.front());
                    m_waiters.pop();

                    if (handle_to_resume.is_valid()) {
                        target_sched = m_scheduler;
                        break;
                    }
                }

                if (!handle_to_resume.is_valid()) {
                    m_locked = false;
                    return;
                }
            }

            if (target_sched) {
                target_sched->schedule(std::move(handle_to_resume));
            } else {
                handle_to_resume.try_resume();
            }
        }
    };
}