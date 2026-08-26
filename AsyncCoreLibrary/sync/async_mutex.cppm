module;

#include <memory>
#include <coroutine>
#include <queue>
#include <mutex>

export module AsyncCoreLibrary:AsyncMutex;

import :ICoroScheduler;

using async_core::coro_loop::ICoroScheduler;

export namespace async_core::sync
{
    class AsyncMutex
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_mtx;
        bool m_locked{false};
        std::queue<std::coroutine_handle<>> m_waiters;

    public:
        explicit AsyncMutex(ICoroScheduler* scheduler = nullptr) noexcept
            : m_scheduler(scheduler) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            m_scheduler = sched;
        }

        class ScopedLock {
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

            ScopedLock(ScopedLock&& other) noexcept : m_mutex(other.m_mutex) {
                other.m_mutex = nullptr;
            }

            ScopedLock& operator=(ScopedLock&& other) noexcept {
                if (this != &other) {
                    if (m_mutex) {
                        m_mutex->unlock();
                    }
                    m_mutex = other.m_mutex;
                    other.m_mutex = nullptr;
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

            bool await_ready() noexcept {
                return m_mutex.try_lock();
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept {
                std::unique_lock lock(m_mutex.m_mtx);

                if (!m_mutex.m_locked) {
                    m_mutex.m_locked = true;
                    return false;
                }

                m_mutex.m_waiters.push(h);
                return true;
            }

            ScopedLock await_resume() noexcept {
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

        [[nodiscard]] auto operator co_await() noexcept {
            return LockAwaiter{*this};
        }

        void unlock() noexcept {
            std::coroutine_handle<> handle_to_resume{nullptr};
            {
                std::unique_lock lock(m_mtx);
                if (!m_waiters.empty()) {
                    handle_to_resume = m_waiters.front();
                    m_waiters.pop();
                } else {
                    m_locked = false;
                }
            }

            if (handle_to_resume && m_scheduler) {
                m_scheduler->schedule(handle_to_resume);
            }
        }
    };
}