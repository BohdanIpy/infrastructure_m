module;

#include <chrono>

export module AsyncCoreLibrary:SleepUntil;

import :TimerQueue;
import :SleepAwaiter;

export namespace async_core::time
{
    class SleepUntil : public SleepAwaiter
    {
    public:
        SleepUntil(TimerQueue& queue, const Clock::time_point deadline) noexcept
            : SleepAwaiter(queue, deadline) {}
    };

    [[nodiscard]] inline SleepUntil sleep_until(TimerQueue& queue, const Clock::time_point deadline) noexcept
    {
        return SleepUntil{queue, deadline};
    }
}