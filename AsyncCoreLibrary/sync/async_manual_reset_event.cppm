module;

#include <coroutine>
#include <mutex>
#include <vector>
#include <utility>

export module AsyncCoreLibrary:AsyncManualResetEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncManualResetEvent
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_mtx;
        std::vector<SafeCoroutineHandle> m_handles;
        bool m_signaled{false};

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

                if (event.m_signaled)
                {
                    return false;
                }

                event.m_handles.push_back(h.promise().get_safe_handle());
                return true;
            }

            void await_resume() const noexcept {}
        };

        explicit AsyncManualResetEvent(ICoroScheduler* scheduler = nullptr, bool signaled = false) noexcept
            : m_scheduler(scheduler), m_signaled(signaled) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            std::lock_guard lock(m_mtx);
            m_scheduler = sched;
        }

        void reset() noexcept
        {
            std::unique_lock lock(m_mtx);
            m_signaled = false;
        }

        void set() noexcept
        {
            std::vector<SafeCoroutineHandle> to_resume;
            ICoroScheduler* target_sched{nullptr};

            {
                std::unique_lock lock(m_mtx);
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
                target_sched = m_scheduler;
            }

            for (auto& safe_h : to_resume)
            {
                if (!safe_h.is_valid()) continue;

                if (target_sched)
                {
                    target_sched->schedule(std::move(safe_h));
                }
                else
                {
                    safe_h.try_resume();
                }
            }
        }

        Awaiter operator co_await() noexcept
        {
            return Awaiter{*this};
        }
    };
}