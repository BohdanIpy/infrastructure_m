module;

#include <coroutine>
#include <exception>
#include <memory>
#include <iostream>

export module AsyncCoreLibrary:DetachedTask;

import :SafeCoroutineHandle;

export namespace async_core::exec
{
    struct DetachedTask {
        struct promise_type {
            std::shared_ptr<CoroutineControlBlock> control = std::make_shared<CoroutineControlBlock>();

            ~promise_type()
            {
                if (control) {
                    control->is_alive.store(false, std::memory_order_release);
                }
            }

            DetachedTask get_return_object() noexcept { return DetachedTask{std::coroutine_handle<promise_type>::from_promise(*this)}; }

            // Starts immediately upon invocation
            std::suspend_never initial_suspend() noexcept { return {}; }

            // Frame destroys itself automatically when finished
            std::suspend_never final_suspend() noexcept { return {}; }

            void return_void() noexcept {}

            void unhandled_exception() noexcept {
                try {
                    std::rethrow_exception(std::current_exception());
                } catch (const std::exception& e) {
                    std::cerr << "[DetachedTask Exception]: " << e.what() << '\n';
                } catch (...) {
                    std::cerr << "[DetachedTask Exception]: Unknown exception\n";
                }
            }

            SafeCoroutineHandle get_safe_handle() {
                return SafeCoroutineHandle{
                    std::coroutine_handle<promise_type>::from_promise(*this),
                    control
                };
            }
        };

        std::coroutine_handle<promise_type> m_handle;

        SafeCoroutineHandle get_safe_handle() const
        {
            return m_handle.promise().get_safe_handle();
        }

        explicit DetachedTask(const std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    };
}
