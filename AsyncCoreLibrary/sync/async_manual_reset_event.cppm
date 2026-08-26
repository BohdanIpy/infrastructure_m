module;

#include <mutex>
#include <queue>
#include <coroutine>

export module AsyncCoreLibrary:AsyncManualResetEvent;

import :ICoroScheduler;

using async_core::coro_loop::ICoroScheduler;

export namespace async_core::sync
{
    class AsyncManualResetEvent
    {
        ICoroScheduler* m_scheduler{nullptr};
        std::mutex m_mtx;
        std::queue<std::coroutine_handle<>> m_handles;
        bool m_signaled{false};

    public:
        explicit AsyncManualResetEvent(ICoroScheduler* scheduler, bool signaled = false) noexcept
            : m_scheduler(scheduler), m_signaled(signaled) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            m_scheduler = sched;
        }

        void reset() noexcept
        {
            std::unique_lock lock(m_mtx);
            m_signaled = false;
        }

        void set() noexcept
        {
            std::vector<std::coroutine_handle<>> to_resume;
            {
                std::unique_lock lock(m_mtx);
                if (m_signaled) return;
                m_signaled = true;

                while (!m_handles.empty())
                {
                    to_resume.push_back(m_handles.front());
                    m_handles.pop();
                }
            }

            if (m_scheduler)
            {
                for (auto h : to_resume)
                {
                    m_scheduler->schedule(h);
                }
            }
        }

        auto operator co_await() noexcept
        {
            struct Awaiter
            {
                AsyncManualResetEvent& event;

                bool await_ready() noexcept
                {
                    std::unique_lock lock(event.m_mtx);
                    return event.m_signaled;
                }

                bool await_suspend(std::coroutine_handle<> h) noexcept
                {
                    std::unique_lock lock(event.m_mtx);
                    if (event.m_signaled)
                    {
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