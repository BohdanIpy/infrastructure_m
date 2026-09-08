module;

#include <atomic>
#include <coroutine>
#include <memory>

export module AsyncCoreLibrary:AsyncLatch;

import :SafeCoroutineHandle;

using async_core::exec::SafeCoroutineHandle;
using async_core::exec::HasSafeHandle;

export namespace async_core::sync
{
    class AsyncLatch
    {
        std::atomic<std::size_t> m_count;
        std::atomic<std::shared_ptr<SafeCoroutineHandle>> m_waiter{nullptr};

    public:
        explicit AsyncLatch(std::size_t count) noexcept
            : m_count(count) {}

        AsyncLatch(const AsyncLatch&) = delete;
        AsyncLatch& operator=(const AsyncLatch&) = delete;

        void count_down(std::size_t n = 1) noexcept
        {
            if (m_count.fetch_sub(n, std::memory_order_acq_rel) == n)
            {
                auto waiter = m_waiter.exchange(nullptr, std::memory_order_acq_rel);
                if (waiter && waiter->is_valid())
                {
                    waiter->try_resume();
                }
            }
        }

        [[nodiscard]] bool is_ready() const noexcept
        {
            return m_count.load(std::memory_order_acquire) == 0;
        }

        bool await_ready() const noexcept
        {
            return is_ready();
        }

        template <typename PromiseT>
            requires HasSafeHandle<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> requesting_coroutine) noexcept
        {
            m_waiter.store(requesting_coroutine.promise().get_safe_handle(), std::memory_order_release);
            if (is_ready())
            {
                if (m_waiter.exchange(nullptr, std::memory_order_acq_rel) != nullptr)
                {
                    return false;
                }
            }
            return true;
        }

        void await_resume() const noexcept {}
    };
}
