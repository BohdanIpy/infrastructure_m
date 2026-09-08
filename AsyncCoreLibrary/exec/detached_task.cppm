module;

#include <coroutine>
#include <exception>
#include <memory>
#include <iostream>
#include <utility>

export module AsyncCoreLibrary:DetachedTask;

import :SafeCoroutineHandle;

export namespace async_core::exec
{
    struct DetachedTask {
        struct promise_type {
            std::shared_ptr<CoroutineControlBlock> control_block{nullptr};
            std::shared_ptr<SafeCoroutineHandle> safe_handle{nullptr};

            ~promise_type() {
                if (control_block) {
                    control_block->state.store(CoroutineState::Destroyed, std::memory_order_release);
                    // if (control_block->raw_handle.done())
                    // {
                    //     control_block->raw_handle.destroy();
                    // }
                }
            }

            DetachedTask get_return_object() noexcept {
                control_block = std::make_shared<CoroutineControlBlock>(typeid(promise_type));
                control_block->state = CoroutineState::Suspended;
                control_block->raw_handle = std::coroutine_handle<promise_type>::from_promise(*this);
                safe_handle = std::make_shared<SafeCoroutineHandle>();
                safe_handle->SetControl(control_block);
                return DetachedTask{safe_handle};
            }

            auto initial_suspend() noexcept { return std::suspend_never{}; }

            auto final_suspend() noexcept { return std::suspend_never{}; }

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

            std::shared_ptr<SafeCoroutineHandle> get_safe_handle() const {
                return safe_handle;
            }
        };

        std::shared_ptr<SafeCoroutineHandle> safe_handle;

        std::shared_ptr<SafeCoroutineHandle> get_safe_handle() const noexcept {
            return safe_handle;
        }

        explicit DetachedTask(std::shared_ptr<SafeCoroutineHandle> handle)
            : safe_handle(std::move(handle)) {}

        ~DetachedTask() = default;
    };
}
