module;

#include <coroutine>
#include <typeinfo>

export module AsyncCoreLibrary:BasicPromiseType;

import :SafeCoroutineHandle;
import :TaskTypes;
import :StartEndPolicy;
import :ReturnValuePolicy;
import :ExceptionHandlingPolicy;
import :ContinuationPolicy;
import :CancellingRoutines;
import :CoroutineStates;

namespace async_core::exec {
using common::CancellationNode;
using common::CoroutineState;
using common::SafeCoroutineHandle;
using common::TaskType;
using policy::ContinuationPolicy;
using policy::ExceptionPolicy;
using policy::LazyStartPolicy;
using policy::ManualEndPolicy;
using policy::PromiseReturnBase;
} // namespace async_core::exec

export namespace async_core::exec {
/**
 * @brief Basic promise_type for task, detached task.
 *
 * @tparam ObjectType Task/DetachedTask, what would we create
 * @tparam ReturnType void/int any type returned by a task
 * @tparam Kind Wired or Detached task
 * @tparam StartPolicy initial_suspend props
 * @tparam EndPolicy final_suspend props
 */
template <typename ObjectType, typename ReturnType, TaskType Kind,
          typename StartPolicy = LazyStartPolicy, typename EndPolicy = ManualEndPolicy>
struct BasicPromiseType final
    : PromiseReturnBase<ReturnType>
    , ExceptionPolicy<Kind>
    , ContinuationPolicy<Kind>
    , CancellationNode {

    SafeCoroutineHandle safe_handle;

    BasicPromiseType() = default;

    ~BasicPromiseType() override { safe_handle.SetState(CoroutineState::Destroyed); }

    ObjectType get_return_object() {
        safe_handle =
            SafeCoroutineHandle{std::coroutine_handle<BasicPromiseType>::from_promise(*this),
                                typeid(BasicPromiseType), m_source};
        return ObjectType{safe_handle};
    }

    auto initial_suspend() noexcept { return StartPolicy::initial_suspend(); }

    auto final_suspend() noexcept { return EndPolicy::template final_suspend<BasicPromiseType>(); }

    [[nodiscard]] SafeCoroutineHandle GetSafeHandle() const noexcept { return safe_handle; }
};
} // namespace async_core::exec
