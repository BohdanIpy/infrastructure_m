module;

#include <coroutine>
#include <semaphore>

export module AsyncCoreLibrary:ContinuationPolicy;

import :TaskTypes;

namespace async_core::policy {
using common::TaskType;
}

export namespace async_core::policy {
// ==========================================
// Execution & Continuation Policies
// ==========================================

template <TaskType Type> struct ContinuationPolicy;

// Exactly one of the two is set while a wired task runs: the coroutine that co_awaits it, or the
// semaphore of a thread blocked in Task::Get().
template <> struct ContinuationPolicy<TaskType::Wired> {
    std::coroutine_handle<> m_continuation{nullptr};
    std::binary_semaphore* m_semaphore{nullptr};
};

template <> struct ContinuationPolicy<TaskType::Detached> {
    // Detached tasks carry no continuation or synchronization semaphores
};
} // namespace async_core::policy
