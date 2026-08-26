module;

#include <coroutine>
#include <exception>

export module AsyncCoreLibrary:DetachedTask;

export namespace async_core::exec
{
    struct DetachedTask {
        struct promise_type {
            DetachedTask get_return_object() noexcept { return {}; }

            std::suspend_never initial_suspend() noexcept { return {}; }

            std::suspend_never final_suspend() noexcept { return {}; }

            void return_void() noexcept {}
            void unhandled_exception() noexcept { std::terminate(); }
        };
    };
}
