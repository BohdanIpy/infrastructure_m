module;

#include <cstdint>

export module AsyncCoreLibrary:CoroutineStates;

export namespace async_core::common {
/**
 * @brief Enum for storing a global coroutine state(lifetime)
 */
enum class CoroutineState : std::uint8_t {
    Created,  // frame allocated, never started
    Started,  // started at least once, frame still alive (running or parked: not distinguished)
    Destroyed // terminal, frame is gone
};
} // namespace async_core::common
