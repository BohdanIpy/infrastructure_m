module;

#include <chrono>
#include <coroutine>

export module AsyncCoreLibrary:SleepUntil;

import :TimerQueue;
import :SafeCoroutineHandle;

using namespace std::chrono;
using async_core::exec::HasSafeHandle;

export namespace async_core::time
{
    using Clock = steady_clock;

    class SleepUntil
    {
        TimerQueue& m_queue;
        const Clock::time_point m_deadline;

    public:

        SleepUntil(TimerQueue& queue, const Clock::time_point& deadline) : m_queue(queue), m_deadline(deadline) {}

        bool await_ready() const noexcept
        {
            return Clock::now() >= m_deadline;
        }

        template <typename PromiseT>
            requires HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) const noexcept
        {
            const auto now = Clock::now();
            const auto wait_time = (m_deadline > now)
                ? std::chrono::duration_cast<milliseconds>(m_deadline - now)
                : milliseconds(0);

            m_queue.AddTimer(h.promise().get_safe_handle(), wait_time);
        }

        void await_resume() const noexcept {}
    };
}
