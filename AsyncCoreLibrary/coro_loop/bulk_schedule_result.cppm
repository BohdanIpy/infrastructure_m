module;

#include <cstdint>

export module AsyncCoreLibrary:BulkScheduleResult;

import :ScheduleTypes;

namespace async_core::coro_loop
{
    using common::ScheduleStatus;
}

export namespace async_core::coro_loop
{
    struct BulkScheduleResult {
        ScheduleStatus status{ScheduleStatus::Success};
        std::uint64_t scheduled{0};
    };
}
