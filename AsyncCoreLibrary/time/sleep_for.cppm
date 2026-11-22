module;

#include <chrono>

export module AsyncCoreLibrary:SleepFor;

import :TimerQueue;
import :SleepAwaiter;

export namespace async_core::time
{
    class SleepFor : public SleepAwaiter
    {
    public:
        SleepFor(TimerQueue& queue, const Clock::duration duration) noexcept
            : SleepAwaiter(queue, detail::deadline_after(duration)) {}
    };

    [[nodiscard]] inline SleepFor sleep_for(TimerQueue& queue, const Clock::duration duration) noexcept
    {
        return SleepFor{queue, duration};
    }
}