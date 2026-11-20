module;

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

export module AsyncCoreLibrary:AsyncLatch;

import :SafeCoroutineHandle;

using async_core::common::SafeCoroutineHandle;
using async_core::common::HasSafeHandleConcept;

export namespace async_core::sync
{
    class AsyncLatch
    {
        std::atomic<std::ptrdiff_t> m_count;
        std::mutex m_mtx;
        std::vector<SafeCoroutineHandle> m_waiters;

    public:
        explicit AsyncLatch(const std::ptrdiff_t count) noexcept
            : m_count(count) {}

        AsyncLatch(const AsyncLatch&) = delete;
        AsyncLatch& operator=(const AsyncLatch&) = delete;

        void count_down(const std::ptrdiff_t n = 1) noexcept
        {
            if (n <= 0) return;

            std::vector<SafeCoroutineHandle> to_resume;
            {
                std::unique_lock lock(m_mtx);
                const auto current = m_count.load(std::memory_order_relaxed);
                if (current <= 0)
                {
                    return;
                }
                const auto next = current - n;
                m_count.store(next, std::memory_order_release);
                if (next <= 0)
                {
                    to_resume = std::move(m_waiters);
                    m_waiters.clear();
                }
            }

            for (auto& waiter : to_resume)
            {
                if (waiter.IsValid())
                {
                    // TODO insert some telemetry, or kind of
                    (void)waiter.TryResume();
                }
            }
        }

        [[nodiscard]] bool is_ready() const noexcept
        {
            return m_count.load(std::memory_order_acquire) <= 0;
        }

        [[nodiscard]] bool try_wait() const noexcept
        {
            return is_ready();
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return is_ready();
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> requesting_coroutine) noexcept
        {
            std::unique_lock lock(m_mtx);
            if (is_ready())
            {
                return false;
            }

            m_waiters.push_back(requesting_coroutine.promise().GetSafeHandle());
            return true;
        }

        void await_resume() const noexcept {}
    };
}
