module;

#include <atomic>
#include <cassert>
#include <coroutine>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncMutex;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :ScheduleTypes;
import :Concepts;

using async_core::coro_loop::ICoroScheduler;
using async_core::common::ScheduleStatus;
using async_core::common::SafeCoroutineHandle;
using async_core::common::HasSafeHandleConcept;

export namespace async_core::sync
{
    class AsyncMutex
    {
        ICoroScheduler& m_scheduler;
        std::atomic<bool> m_locked{false};
        std::mutex m_mtx;
        std::queue<SafeCoroutineHandle> m_waiters;

    public:
        explicit AsyncMutex(ICoroScheduler& scheduler) noexcept
            : m_scheduler(scheduler) {}

        AsyncMutex(const AsyncMutex&) = delete;
        AsyncMutex& operator=(const AsyncMutex&) = delete;
        AsyncMutex(AsyncMutex&&) noexcept = delete;
        AsyncMutex& operator=(AsyncMutex&&) noexcept = delete;

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
                requires HasSafeHandleConcept<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
                std::unique_lock lock(m_mutex.m_mtx);

                if (!m_mutex.m_locked.load(std::memory_order_relaxed) && m_mutex.m_waiters.empty()) {
                    m_mutex.m_locked.store(true, std::memory_order_release);
                    return false;
                }

                m_mutex.m_waiters.push(h.promise().GetSafeHandle());
                return true;
            }

            ScopedLock await_resume() noexcept {
                return ScopedLock(&m_mutex);
            }
        };

        bool try_lock() noexcept {
            std::unique_lock lock(m_mtx);
            if (m_locked.load(std::memory_order_relaxed) || !m_waiters.empty()) {
                return false;
            }
            m_locked.store(true, std::memory_order_release);
            return true;
        }

        [[nodiscard]] LockAwaiter lock_async() noexcept {
            return LockAwaiter{*this};
        }

        [[nodiscard]] LockAwaiter operator co_await() noexcept {
            return LockAwaiter{*this};
        }

        void unlock() noexcept {
            while (true) {
                SafeCoroutineHandle handle_to_resume{};
                {
                    std::unique_lock lock(m_mtx);
                    assert(m_locked.load(std::memory_order_relaxed) && "Double unlock detected on AsyncMutex");

                    while (!m_waiters.empty()) {
                        handle_to_resume = std::move(m_waiters.front());
                        m_waiters.pop();

                        if (handle_to_resume.IsValid()) {
                            break;
                        }
                    }

                    if (!handle_to_resume.IsValid()) {
                        m_locked.store(false, std::memory_order_release);
                        return;
                    }
                }

                const ScheduleStatus status = m_scheduler.schedule(std::move(handle_to_resume));

                if (status == ScheduleStatus::Success) {
                    return;
                }

                if (status == ScheduleStatus::InvalidHandle) {
                    continue;
                }

                if (status == ScheduleStatus::SchedulerStopped) {
                    std::unique_lock lock(m_mtx);
                    if (m_waiters.empty()) {
                        m_locked.store(false, std::memory_order_release);
                    } else {
                        lock.unlock();
                        continue;
                    }
                    return;
                }

                return;
            }
        }
    };
}