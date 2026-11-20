module;

#include <cstdint>

export module AsyncCoreLibrary:CoroutineStates;

export namespace async_core::common
{
    enum class CoroutineState : uint8_t {
        Created,    // frame allocated, never started
        Started,    // started at least once, frame still alive (running or parked: not distinguished)
        Destroyed   // terminal, frame is gone
    };

    enum class AwaitableState : uint8_t {
        Pending = 0,
        Completed = 1,
        Cancelled = 2
    };
}
