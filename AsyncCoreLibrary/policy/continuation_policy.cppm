module;

#include <coroutine>
#include <semaphore>

export module AsyncCoreLibrary:ContinuationPolicy;

import :TaskTypes;

using async_core::common::TaskType;

export namespace async_core::policy
{
    // ==========================================
    // Execution & Continuation Policies
    // ==========================================

    template <TaskType Type>
    struct ContinuationPolicy;

    template <>
    struct ContinuationPolicy<TaskType::Wired>
    {
        std::coroutine_handle<> continuation{nullptr};
        std::binary_semaphore* m_semaphore{nullptr};
    };

    template <>
    struct ContinuationPolicy<TaskType::Detached>
    {
        // Detached tasks carry no continuation or synchronization semaphores
    };
}