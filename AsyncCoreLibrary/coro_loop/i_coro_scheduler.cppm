module;

#include <coroutine>

export module AsyncCoreLibrary:ICoroScheduler;

import :SafeCoroutineHandle;

export namespace async_core::coro_loop
{
    class ICoroScheduler
    {
    public:
        virtual void schedule(exec::SafeCoroutineHandle safeHandle) noexcept = 0;

        virtual void scheduleOnPosition(exec::SafeCoroutineHandle safeHandle, std::size_t pos) noexcept = 0;

        virtual void stop_and_wait_all() = 0;

        virtual void request_stop() = 0;

        virtual ~ICoroScheduler() = default;
    };
}
