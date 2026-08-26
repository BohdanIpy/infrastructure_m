module;

#include <coroutine>

export module AsyncCoreLibrary:ICoroScheduler;

export namespace async_core::coro_loop
{
    class ICoroScheduler
    {
    public:
        virtual void schedule(std::coroutine_handle<> h) noexcept = 0;

        virtual void stop_and_wait_all() = 0;

        virtual void request_stop() = 0;

        virtual ~ICoroScheduler() = default;
    };
}
