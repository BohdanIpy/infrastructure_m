module;

#include <coroutine>
#include <memory>
#include <span>

export module AsyncCoreLibrary:ICoroScheduler;
export import :ScheduleTypes;

import :SafeCoroutineHandle;
import :BulkScheduleResult;

namespace async_core::coro_loop
{
    using common::SafeCoroutineHandle;
    using common::ScheduleStatus;
}

export namespace async_core::coro_loop
{
    class ICoroScheduler
    {
    public:
        [[nodiscard]]
        virtual ScheduleStatus schedule(SafeCoroutineHandle safeHandle) noexcept = 0;

        [[nodiscard]]
        virtual ScheduleStatus scheduleOnPosition(SafeCoroutineHandle safeHandle, std::uint64_t pos) noexcept = 0;

        [[nodiscard]]
        virtual BulkScheduleResult schedule_bulk(std::span<const SafeCoroutineHandle> handles) noexcept = 0;

        [[nodiscard]]
        virtual std::uint64_t worker_count() const noexcept = 0;

        virtual void stop_and_wait_all() = 0;

        virtual void request_stop() noexcept = 0;

        virtual ~ICoroScheduler() = default;
    };
}
