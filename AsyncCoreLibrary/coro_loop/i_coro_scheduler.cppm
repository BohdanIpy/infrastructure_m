module;

#include <cstdint>
#include <span>

export module AsyncCoreLibrary:ICoroScheduler;
export import :ScheduleTypes;

import :SafeCoroutineHandle;
import :BulkScheduleResult;

namespace async_core::coro_loop {
using common::SafeCoroutineHandle;
using common::ScheduleStatus;
} // namespace async_core::coro_loop

export namespace async_core::coro_loop {
class ICoroScheduler {
  public:
    /**
     * @brief Schedule one coroutine on the scheduler
     *
     * @param safeHandle handle to the coroutine to be scheduled
     * @return ScheduleStatus success or failure of sceduling
     */
    [[nodiscard]]
    virtual ScheduleStatus Schedule(SafeCoroutineHandle safeHandle) noexcept = 0;

    /**
     * @brief Schedule one coroutine on specified thread of scheduler
     *
     * @param safeHandle handle to the coroutine to be scheduled
     * @param pos position of thread
     * @return ScheduleStatus success or failure of sceduling
     */
    [[nodiscard]]
    virtual ScheduleStatus ScheduleOnPosition(SafeCoroutineHandle safeHandle,
                                              std::uint64_t pos) noexcept = 0;
    /**
     * @brief Schedules multiple coroutine handles
     *
     * @param handles span of coroutine handles
     * @return BulkScheduleResult result of scheduling, if not all scheduled, returns number how
     * much was successfully scheduled
     */
    [[nodiscard]]
    virtual BulkScheduleResult
    ScheduleBulk(std::span<const SafeCoroutineHandle> handles) noexcept = 0;

    /**
     * @brief Returns number of worker threads inside a scheduler
     * 
     * @return std::uint64_t number of threads
     */
    [[nodiscard]]
    virtual std::uint64_t WorkerCount() const noexcept = 0;

    /**
     * @brief RequestStop with waiting for completion of draining 
     * 
     */
    virtual void StopAndWaitAll() = 0;

    /**
     * @brief Only Stop the scheduler and drain all the presented coroutine handles
     * 
     */
    virtual void RequestStop() noexcept = 0;

    virtual ~ICoroScheduler() = default;
};
} // namespace async_core::coro_loop
