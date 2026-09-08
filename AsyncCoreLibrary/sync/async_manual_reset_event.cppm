module;

#include <coroutine>
#include <mutex>
#include <vector>
#include <utility>
#include <memory>
#include <atomic>

export module AsyncCoreLibrary:AsyncManualResetEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncManualResetEvent
    {
        ICoroScheduler& m_scheduler;

        std::mutex m_mtx;
        std::vector<std::shared_ptr<SafeCoroutineHandle>> m_handles;

        std::atomic<bool> m_signaled{false};

    public:
        struct Awaiter
        {
            AsyncManualResetEvent& event;

            bool await_ready() const noexcept { return false; }

            template <typename Promise>
                requires exec::HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                std::unique_lock lock(event.m_mtx);

                if (event.m_signaled.load(std::memory_order::acquire))
                {
                    return false;
                }

                event.m_handles.push_back(h.promise().get_safe_handle());
                return true;
            }

            void await_resume() const noexcept {}
        };

        explicit AsyncManualResetEvent(ICoroScheduler& scheduler, bool signaled = false) noexcept
            : m_scheduler(scheduler), m_signaled(signaled) {}

        void reset() noexcept
        {
            std::unique_lock lock(m_mtx);
            m_signaled.store(false, std::memory_order::release);
        }

        void set() noexcept
        {
            std::vector<std::shared_ptr<SafeCoroutineHandle>> to_resume;
            {
                std::unique_lock lock(m_mtx);
                if (m_signaled.load(std::memory_order::acquire)) return;
                m_signaled.store(true, std::memory_order::release);

                for (auto& safe_h : m_handles)
                {
                    if (safe_h->is_valid())
                    {
                        to_resume.push_back(std::move(safe_h));
                    }
                }
                m_handles.clear();
            }

            for (auto& safe_h : to_resume)
            {
                if (!safe_h->is_valid()) continue;

                m_scheduler.schedule(std::move(safe_h));
            }
        }

        Awaiter operator co_await() noexcept
        {
            return Awaiter{*this};
        }
    };
}