module;

#include <utility>
#include <cassert>

export module AsyncCoreLibrary:TaskBase;

import :SafeCoroutineHandle;
import :TaskTypes;
import :CoroutineStates;
import :Concepts;

namespace async_core::exec
{
    using common::SafeCoroutineHandle;
    using common::IsWiredTaskEsConcept;
    using common::CoroutineState;
}

export namespace async_core::exec
{
    template <typename TaskTypeT>
    class TaskBase {
    public:
        using task_type = TaskTypeT;
        static constexpr bool owns_frame = IsWiredTaskEsConcept<TaskTypeT>;
    protected:
        SafeCoroutineHandle safe_handle;
        void destroy_frame() noexcept {
            if constexpr (owns_frame) {
                assert((!safe_handle.IsValid() || safe_handle.LoadState() == CoroutineState::Created || safe_handle.IsDone())
                       && "Task destroyed while its coroutine may still be resumed: RequestStop() and wait for completion first");
                (void)safe_handle.Destroy();
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

        ~TaskBase()
        {
            destroy_frame();
        }

        [[nodiscard]] SafeCoroutineHandle GetSafeHandle() const noexcept {
            return safe_handle;
        }

        [[nodiscard]] bool IsValid() const noexcept {
            return safe_handle.IsValid();
        }

        void TryResume() const
        {
            if (safe_handle.IsValid())
            {
                (void)safe_handle.TryResume();
            }
        }

        bool TryStart() const
        {
            if (safe_handle.IsValid())
            {
                return safe_handle.TryStart();
            }
            return false;
        }
    };
}