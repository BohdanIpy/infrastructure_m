module;

#include <cstdint>

export module AsyncCoreLibrary:BulkScheduleResult;

import :ScheduleTypes;

namespace async_core::coro_loop {
using common::ScheduleStatus;
}

export namespace async_core::coro_loop {
/**
 * @brief Struct for deploying multiple tasks into a ICoroScheduler
 * holds a status of scheduling, and how much was scheduled successfully
 */
struct BulkScheduleResult {
    ScheduleStatus status{ScheduleStatus::Success};
    std::uint64_t scheduled{0};
};
} // namespace async_core::coro_loop
