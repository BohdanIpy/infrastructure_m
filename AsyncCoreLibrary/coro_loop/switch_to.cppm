module;

#include <coroutine>

export module AsyncCoreLibrary:SwitchTo;

import :ICoroScheduler;

export namespace async_core::coro_loop
{
    struct SwitchTo
    {
        ICoroScheduler* scheduler;

        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> coroutine) noexcept
        {
            scheduler->schedule(coroutine);
        }

        void await_resume() const noexcept
        {

        }
    };
}
