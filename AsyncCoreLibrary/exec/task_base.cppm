module;

#include <utility>
#include <cassert>

export module AsyncCoreLibrary:TaskBase;

import :SafeCoroutineHandle;
import :TaskTypes;
import :CoroutineStates;
import :Concepts;

namespace async_core::exec {
using common::CoroutineState;
using common::IsWiredTaskEsConcept;
using common::SafeCoroutineHandle;
} // namespace async_core::exec

export namespace async_core::exec {
template <typename TaskTypeT> class TaskBase {
  public:
    using task_type = TaskTypeT;
    static constexpr bool owns_frame = IsWiredTaskEsConcept<TaskTypeT>;

  protected:
    SafeCoroutineHandle m_safeHandle;
    void destroy_frame() noexcept {
        if constexpr (owns_frame) {
            assert((!m_safeHandle.IsValid() ||
                    m_safeHandle.LoadState() == CoroutineState::Created || m_safeHandle.IsDone()) &&
                   "Task destroyed while its coroutine may still be resumed: RequestStop() and "
                   "wait for completion first");
            (void)m_safeHandle.Destroy();
        }
    }

  public:
    explicit TaskBase(SafeCoroutineHandle handle)
        : m_safeHandle(std::move(handle)) {}

    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;

    TaskBase(TaskBase&& other) noexcept
        : m_safeHandle(std::exchange(other.m_safeHandle, SafeCoroutineHandle{})) {}

    TaskBase& operator=(TaskBase&& other) noexcept {
        if (this != &other) {
            destroy_frame();
            m_safeHandle = std::exchange(other.m_safeHandle, SafeCoroutineHandle{});
        }
        return *this;
    }

    ~TaskBase() { destroy_frame(); }

    [[nodiscard]] SafeCoroutineHandle GetSafeHandle() const noexcept { return m_safeHandle; }

    [[nodiscard]] bool IsValid() const noexcept { return m_safeHandle.IsValid(); }

    void TryResume() const {
        if (m_safeHandle.IsValid()) {
            (void)m_safeHandle.TryResume();
        }
    }

    [[nodiscard]] bool TryStart() const {
        if (m_safeHandle.IsValid()) {
            return m_safeHandle.TryStart();
        }
        return false;
    }
};
} // namespace async_core::exec