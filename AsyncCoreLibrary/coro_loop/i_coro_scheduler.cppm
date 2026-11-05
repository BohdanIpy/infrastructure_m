module;

#include <coroutine>
#include <memory>
#include <span>

export module AsyncCoreLibrary:ICoroScheduler;

import :SafeCoroutineHandle;
export import :ScheduleTypes;

using async_core::common::SafeCoroutineHandle;
using async_core::common::ScheduleStatus;

export namespace async_core::coro_loop
{
    class ICoroScheduler
    {
    public:
        [[nodiscard]]
        virtual ScheduleStatus schedule(SafeCoroutineHandle safeHandle) noexcept = 0;

        [[nodiscard]]
        virtual ScheduleStatus scheduleOnPosition(SafeCoroutineHandle safeHandle, std::size_t pos) noexcept = 0;

        [[nodiscard]]
        virtual ScheduleStatus schedule_bulk(std::span<const SafeCoroutineHandle> handles) noexcept = 0;

        [[nodiscard]]
        virtual std::size_t worker_count() const noexcept = 0;

        virtual void stop_and_wait_all() = 0;

        virtual void request_stop() noexcept = 0;

        virtual ~ICoroScheduler() = default;
    };
}
