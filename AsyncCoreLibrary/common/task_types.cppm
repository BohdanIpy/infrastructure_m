module;

#include <cstdint>

export module AsyncCoreLibrary:TaskTypes;

export namespace async_core::common {
/**
 * @brief empty structs for compile time restrictions
 */
struct WiredTaskES {};
/**
 * @brief empty structs for compile time restrictions
 */
struct DetachedTaskES {};

/**
 * @brief Stores the enum for showing if the coroutine is Wired a.k.a Task<>, or is Detached a.k.a
 * DetachedTask<>
 */
enum class TaskType : uint8_t {
    Wired = 0,
    Detached = 1,
};
} // namespace async_core::common
