module;

#include <coroutine>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

export module AsyncCoreLibrary:AsyncManualResetEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::common::SafeCoroutineHandle;
using async_core::common::HasSafeHandle;

export namespace async_core::sync
{
    class AsyncManualResetEvent
    {
        ICoroScheduler& m_scheduler;

        std::mutex m_mtx;
        std::vector<SafeCoroutineHandle> m_handles;
        bool m_signaled{false};

    public:
        struct Awaiter
        {
            AsyncManualResetEvent& event;

            bool await_ready() const noexcept
            {
                std::lock_guard lock(event.m_mtx);
                return event.m_signaled;
            }

            template <typename Promise>
                requires HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                std::unique_lock lock(event.m_mtx);

                if (event.m_signaled)
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

        AsyncManualResetEvent(const AsyncManualResetEvent&) = delete;
        AsyncManualResetEvent& operator=(const AsyncManualResetEvent&) = delete;
        AsyncManualResetEvent(AsyncManualResetEvent&&) noexcept = delete;
        AsyncManualResetEvent& operator=(AsyncManualResetEvent&&) noexcept = delete;

        void reset() noexcept
        {
            std::lock_guard lock(m_mtx);
            m_signaled = false;
        }

        void set() noexcept
        {
            std::vector<SafeCoroutineHandle> to_resume;
            {
                std::lock_guard lock(m_mtx);
                if (m_signaled) return;
                m_signaled = true;

                for (auto& safe_h : m_handles)
                {
                    if (safe_h.is_valid())
                    {
                        to_resume.push_back(std::move(safe_h));
                    }
                }
                m_handles.clear();
            }

            for (auto& safe_h : to_resume)
            {
                if (safe_h.is_valid())
                {
                    // TODO insert some telemetry, or kind of
                    (void)m_scheduler.schedule(std::move(safe_h));
                }
            }
        }

        Awaiter operator co_await() noexcept
        {
            return Awaiter{*this};
        }
    };
}