module;

#include <coroutine>
#include <mutex>
#include <queue>
#include <utility>

export module AsyncCoreLibrary:AsyncAutoResetEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    class AsyncAutoResetEvent
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_mtx;
        std::queue<SafeCoroutineHandle> m_handles;
        bool m_signaled{false};

    public:
        struct Awaiter
        {
            AsyncAutoResetEvent* event;

            bool await_ready() noexcept { return false; }

            template <typename Promise>
                requires exec::HasSafeHandle<Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                std::unique_lock lock(event->m_mtx);

                if (event->m_signaled)
                {
                    event->m_signaled = false;
                    return false;
                }

                event->m_handles.push(h.promise().get_safe_handle());
                return true;
            }

            void await_resume() const noexcept {}
        };

        explicit AsyncAutoResetEvent(ICoroScheduler* sched = nullptr, bool initially_signaled = false)
            : m_scheduler(sched), m_signaled(initially_signaled) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            std::lock_guard lock(m_mtx);
            m_scheduler = sched;
        }

        void set() noexcept
        {
            SafeCoroutineHandle safe_h;
            ICoroScheduler* target_sched{nullptr};

            {
                std::unique_lock lock(m_mtx);

                while (!m_handles.empty())
                {
                    safe_h = std::move(m_handles.front());
                    m_handles.pop();

                    if (safe_h.is_valid()) {
                        target_sched = m_scheduler;
                        break;
                    }
                }

                if (!safe_h.is_valid())
                {
                    m_signaled = true;
                    return;
                }
            }

            if (target_sched)
            {
                target_sched->schedule(std::move(safe_h));
            }
            else
            {
                safe_h.try_resume();
            }
        }

        Awaiter operator co_await() noexcept
        {
            return Awaiter{this};
        }
    };
}