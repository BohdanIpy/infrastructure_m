module;

#include <cstdint>

export module AsyncCoreLibrary:ScheduleTypes;

export namespace async_core::coro_loop
{
    enum class ScheduleStatus : uint8_t {
        Success,
        SchedulerStopped,
        InvalidHandle
    };
}
