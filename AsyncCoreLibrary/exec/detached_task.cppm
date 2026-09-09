module;

#include <coroutine>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

export module AsyncCoreLibrary:DetachedTask;

import :SafeCoroutineHandle;

export namespace async_core::exec
{
    template <typename StreamType = std::ostream&>
    struct BasicDetachedTask {
        struct promise_type {
            std::shared_ptr<CoroutineControlBlock> control_block{nullptr};
            SafeCoroutineHandle safe_handle;
            StreamType instance = std::cerr;

            ~promise_type() {
                if (control_block) {
                    control_block->state.store(CoroutineState::Destroyed, std::memory_order_release);
                    control_block->raw_handle = nullptr;
                }
            }

            BasicDetachedTask get_return_object() noexcept {
                control_block = std::make_shared<CoroutineControlBlock>(typeid(promise_type));
                control_block->state = CoroutineState::Suspended;
                control_block->raw_handle = std::coroutine_handle<promise_type>::from_promise(*this);
                safe_handle.SetControl(control_block);
                return BasicDetachedTask{safe_handle};
            }

            auto initial_suspend() noexcept { return std::suspend_never{}; }

            auto final_suspend() noexcept { return std::suspend_never{}; }

            void return_void() noexcept {}

            void unhandled_exception() noexcept {
                try {
                    std::rethrow_exception(std::current_exception());
                } catch (const std::exception& e) {
                    instance << "[DetachedTask Exception]: " << e.what() << '\n';
                } catch (...) {
                    instance << "[DetachedTask Exception]: Unknown exception\n";
                }
            }

            SafeCoroutineHandle get_safe_handle() const {
                return safe_handle;
            }
        };

        SafeCoroutineHandle safe_handle;

        SafeCoroutineHandle get_safe_handle() const noexcept {
            return safe_handle;
        }

        explicit BasicDetachedTask(SafeCoroutineHandle handle)
            : safe_handle(std::move(handle)) {}

        ~BasicDetachedTask() = default;
    };

    using DetachedTask = BasicDetachedTask<>;
}
