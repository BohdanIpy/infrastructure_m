module;

#include <utility>
#include <stop_token>

export module AsyncCoreLibrary:TaskBase;

import :SafeCoroutineHandle;

using async_core::common::SafeCoroutineHandle;

export namespace async_core::exec
{
    template <typename Derived, typename PromiseType>
    class TaskBase
    {
    protected:
        SafeCoroutineHandle safe_handle;

        /**
         * function required for safely destroy a coroutine frame ussing a coroutine_handle<>
         */
        void destroy_frame() noexcept {
            if (safe_handle.is_valid()) {
                auto h = safe_handle.template raw_handle<PromiseType>();
                safe_handle = SafeCoroutineHandle{};
                if (h) {
                    h.destroy();
                }
            }
        }

    public:
        explicit TaskBase(SafeCoroutineHandle handle) : safe_handle(std::move(handle)) {}

        TaskBase(const TaskBase&) = delete;
        TaskBase& operator=(const TaskBase&) = delete;

        TaskBase(TaskBase&& other) noexcept
            : safe_handle(std::exchange(other.safe_handle, SafeCoroutineHandle{})) {}

        TaskBase& operator=(TaskBase&& other) noexcept {
            if (this != &other) {
                destroy_frame();
                safe_handle = std::exchange(other.safe_handle, SafeCoroutineHandle{});
            }
            return *this;
        }

        ~TaskBase() = default;

        [[nodiscard]] SafeCoroutineHandle get_safe_handle() const noexcept {
            return safe_handle;
        }

        void cancel() const noexcept {
            if (safe_handle.is_valid()) {
                safe_handle.template raw_handle<PromiseType>().promise().request_stop();
            }
        }

        void bind_stop_token(const std::stop_token& stop_token) noexcept
        {
            if (safe_handle.is_valid())
            {
                safe_handle.template raw_handle<PromiseType>().promise().add_parent_stop_token(stop_token);
            }
        }

        [[nodiscard]] bool is_cancelled() const noexcept {
            if (safe_handle.is_valid()) {
                return safe_handle.template raw_handle<PromiseType>().promise().is_cancelled();
            }
            return false;
        }

        [[nodiscard]] bool is_valid() const noexcept {
            return safe_handle.is_valid();
        }

        void try_resume() const
        {
            if (safe_handle.is_valid())
            {
                (void)safe_handle.try_resume();
            }
        }
    };
}