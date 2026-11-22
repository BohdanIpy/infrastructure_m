module;

#include <cstdint>
#include <string_view>

export module AsyncCoreLibrary:ScheduleTypes;

export namespace async_core::common {
/**
 * @brief Members for schduler to determine if scheduling was seccessful or not
 *
 */
enum class ScheduleStatus : std::uint8_t { Success, SchedulerStopped, InvalidHandle, OutOfMemory };
/**
 * @brief convert enum type to string
 *
 * @param status
 * @return constexpr std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(ScheduleStatus status) noexcept {
    switch (status) {
        case ScheduleStatus::Success:
            return "Success";
        case ScheduleStatus::SchedulerStopped:
            return "SchedulerStopped";
        case ScheduleStatus::InvalidHandle:
            return "InvalidHandle";
        case ScheduleStatus::OutOfMemory:
            return "OutOfMemory";
    }
    return "Unknown";
}
} // namespace async_core::common
