module;

#include <cstdint>
#include <concepts>

export module AsyncCoreLibrary:TaskTypes;

export namespace async_core::common
{
    /**
     * empty structs for compile time restrictions
     */
    struct WiredTaskES{};
    struct DetachedTaskES{};

    /**
     * Stores the enum for showing if the coroutine is Wired a.k.a Task<>, or is Detached a.k.a DetachedTask<>
     */
    enum class TaskType : uint8_t
    {
        Wired = 0,
        Detached = 1,
    };
}
