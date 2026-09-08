module;

#include <coroutine>
#include <mutex>
#include <queue>
#include <memory>
#include <utility>
#include <cassert>
#include <atomic>

export module AsyncCoreLibrary:AsyncAutoResetEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncAutoResetEvent
    {
        ICoroScheduler& m_scheduler;

        std::mutex m_mtx;
        std::queue<std::shared_ptr<SafeCoroutineHandle>> m_handles;

        std::atomic<bool> m_signaled{false};

    public:
        struct Awaiter
        {
            AsyncAutoResetEvent& event;

            bool await_ready() noexcept { return false; }

            template <typename Promise>
                requires exec::HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                std::unique_lock lock(event.m_mtx);

                if (event.m_signaled.load(std::memory_order_acquire))
                {
                    event.m_signaled.store(false, std::memory_order_release);
                    return false;
                }

                auto safe_h = h.promise().get_safe_handle();
                event.m_handles.push(std::move(safe_h));

                return true;
            }

            void await_resume() const noexcept {}
        };

        explicit AsyncAutoResetEvent(ICoroScheduler& sched, bool initially_signaled = false)
            : m_scheduler(sched), m_signaled(initially_signaled) {}

        void set() noexcept
        {
            std::shared_ptr<SafeCoroutineHandle> safe_h = nullptr;

            {
                std::unique_lock lock(m_mtx);

                while (!m_handles.empty())
                {
                    safe_h = std::move(m_handles.front());
                    m_handles.pop();

                    if (safe_h && safe_h->is_valid())
                    {
                        break;
                    }
                    safe_h = nullptr;
                }

                if (!safe_h)
                {
                    m_signaled.store(true, std::memory_order_release);
                    return;
                }
            }
            m_scheduler.schedule(std::move(safe_h));
        }

        Awaiter operator co_await() noexcept
        {
            return Awaiter{*this};
        }
    };
}