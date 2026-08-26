module;

#include <coroutine>
#include <mutex>
#include <queue>

export module AsyncCoreLibrary:AsyncAutoResetEvent;

import :ICoroScheduler;

using async_core::coro_loop::ICoroScheduler;

export namespace async_core::sync
{
    class AsyncAutoResetEvent
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_mtx;
        std::queue<std::coroutine_handle<>> m_handles;
        bool m_signaled{false};

    public:
        explicit AsyncAutoResetEvent(ICoroScheduler* sched = nullptr, bool initially_signaled = false)
            : m_scheduler(sched), m_signaled(initially_signaled) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            m_scheduler = sched;
        }

        void set() noexcept
        {
            std::coroutine_handle<> h{nullptr};
            {
                std::unique_lock lock(m_mtx);
                if (!m_handles.empty())
                {
                    h = m_handles.front();
                    m_handles.pop();
                }
                else
                {
                    m_signaled = true;
                    return;
                }
            }

            if (h && m_scheduler)
            {
                m_scheduler->schedule(h);
            }
        }

        auto operator co_await() noexcept
        {
            struct Awaiter
            {
                AsyncAutoResetEvent& event;

                bool await_ready() noexcept
                {
                    std::unique_lock lock(event.m_mtx);
                    if (event.m_signaled)
                    {
                        event.m_signaled = false;
                        return true;
                    }
                    return false;
                }

                bool await_suspend(std::coroutine_handle<> h) noexcept
                {
                    std::unique_lock lock(event.m_mtx);
                    if (event.m_signaled)
                    {
                        event.m_signaled = false;
                        return false;
                    }

                    event.m_handles.push(h);
                    return true;
                }

                void await_resume() const noexcept {}
            };

            return Awaiter{*this};
        }
    };

}
