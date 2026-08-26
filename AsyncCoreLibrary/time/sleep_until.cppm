module;

#include <chrono>
#include <algorithm>
#include <coroutine>

export module AsyncCoreLibrary:SleepUntil;

import :TimerQueue;

using namespace std::chrono;

export namespace async_core::time
{
    using Clock = steady_clock;

    class SleepUntil
    {
        TimerQueue& m_queue;
        Clock::time_point m_deadline;

    public:

        SleepUntil(TimerQueue& queue, const Clock::time_point& deadline) : m_queue(queue), m_deadline(deadline) {}

        bool await_ready() const noexcept
        {
            return Clock::now() >= m_deadline;
        }

        void await_suspend(std::coroutine_handle<> h) const noexcept
        {
            auto now = Clock::now();
            auto wait_time = (m_deadline > now)
                ? std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - now)
                : milliseconds(0);

            m_queue.AddTimer(h, wait_time);
        }

        void await_resume() const noexcept {}
    };
}
