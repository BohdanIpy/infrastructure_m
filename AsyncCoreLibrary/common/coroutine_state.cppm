module;

#include <cstdint>

export module AsyncCoreLibrary:CoroutineStates;

export namespace async_core::common
{
    enum class CoroutineState : uint8_t {
        Created,    // Allocated, but never resumed/started
        Suspended,  // Suspended at an await point
        Running,    // Actively executing
        Cancelled,  // Cancelled by parent or user
        Destroyed   // Destroyed or completed
    };

    enum class AwaitableState : uint8_t {
        Pending = 0,
        Completed = 1,
        Cancelled = 2
    };
}
