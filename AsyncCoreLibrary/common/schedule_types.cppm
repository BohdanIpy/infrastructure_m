module;

#include <cstdint>
#include <string_view>

export module AsyncCoreLibrary:ScheduleTypes;

export namespace async_core::common
{
    enum class ScheduleStatus : uint8_t {
        Success,
        SchedulerStopped,
        InvalidHandle,
        OutOfMemory
    };

    [[nodiscard]] constexpr std::string_view to_string(ScheduleStatus status) noexcept {
        switch (status) {
        case ScheduleStatus::Success:          return "Success";
        case ScheduleStatus::SchedulerStopped: return "SchedulerStopped";
        case ScheduleStatus::InvalidHandle:    return "InvalidHandle";
        case ScheduleStatus::OutOfMemory:      return "OutOfMemory";
        }
        return "Unknown";
    }
}
