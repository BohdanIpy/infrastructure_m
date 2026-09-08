module;

#include <coroutine>
#include <mutex>
#include <queue>
#include <utility>
#include <memory>
#include <atomic>
#include <cassert>

export module AsyncCoreLibrary:AsyncMutex;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncMutex
    {
        ICoroScheduler& m_scheduler;
        std::atomic<bool> m_locked{false};
        std::mutex m_mtx;
        std::queue<std::shared_ptr<SafeCoroutineHandle>> m_waiters;

    public:
        explicit AsyncMutex(ICoroScheduler& scheduler) noexcept
            : m_scheduler(scheduler) {}

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

            [[nodiscard]] bool owns_lock() const noexcept {
                return m_mutex != nullptr;
            }
        };

        struct LockAwaiter
        {
            AsyncMutex& m_mutex;

            bool await_ready() noexcept {
                return m_mutex.try_lock();
            }

            template <typename Promise>
                requires exec::HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
                std::unique_lock lock(m_mutex.m_mtx);

                if (!m_mutex.m_locked.load(std::memory_order_relaxed)) {
                    m_mutex.m_locked.store(true, std::memory_order_release);
                    return false;
                }

                auto safe_h = h.promise().get_safe_handle();
                m_mutex.m_waiters.push(std::move(safe_h));
                return true;
            }

            ScopedLock await_resume() noexcept {
                return ScopedLock(&m_mutex);
            }
        };

        bool try_lock() noexcept {
            bool expected = false;
            return m_locked.compare_exchange_strong(
                expected, true,
                std::memory_order_acquire,
                std::memory_order_relaxed
            );
        }

        [[nodiscard]] LockAwaiter lock_async() noexcept {
            return LockAwaiter{*this};
        }

        [[nodiscard]] LockAwaiter operator co_await() noexcept {
            return LockAwaiter{*this};
        }

        void unlock() noexcept {
            std::shared_ptr<SafeCoroutineHandle> handle_to_resume = nullptr;
            {
                std::unique_lock lock(m_mtx);
                assert(m_locked.load(std::memory_order_relaxed) && "Double unlock detected on AsyncMutex");
                while (!m_waiters.empty()) {
                    handle_to_resume = std::move(m_waiters.front());
                    m_waiters.pop();

                    if (handle_to_resume && handle_to_resume->is_valid()) {
                        break;
                    }
                    handle_to_resume = nullptr;
                }
                if (!handle_to_resume) {
                    m_locked.store(false, std::memory_order_release);
                    return;
                }
            }
            m_scheduler.schedule(std::move(handle_to_resume));
        }
    };
}