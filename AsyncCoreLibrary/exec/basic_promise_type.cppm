module;

#include <coroutine>
#include <exception>
#include <type_traits>
#include <typeinfo>
#include <utility>

export module AsyncCoreLibrary:BasicPromiseType;

import :SafeCoroutineHandle;
import :TaskTypes;
import :StartEndPolicy;
import :ReturnValuePolicy;
import :ExceptionHandlingPolicy;
import :ContinuationPolicy;
import :CancellingRoutines;
import :CoroutineStates;
import :Exceptions;

namespace async_core::exec {
using common::CancellationNode;
using common::CoroutineState;
using common::OperationCancelledException;
using common::SafeCoroutineHandle;
using common::TaskFinishedWithoutProducingValueException;
using common::TaskReturnedNullReferenceException;
using common::TaskType;
using policy::ContinuationPolicy;
using policy::ExceptionPolicy;
using policy::IsEndPolicyConcept;
using policy::IsStartPolicyConcept;
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

    static_assert(IsStartPolicyConcept<StartPolicy>,
                  "StartPolicy must provide initial_suspend() and starts_immediately");
    static_assert(IsEndPolicyConcept<EndPolicy>, "EndPolicy must provide final_suspend<Promise>()");
    static_assert(Kind != TaskType::Wired || !StartPolicy::starts_immediately,
                  "a wired Task must be lazy: an already running coroutine cannot be awaited later "
                  "(it would be resumed a second time). Use DetachedTask for eager start.");

    SafeCoroutineHandle m_safeHandle;

    BasicPromiseType() = default;

    ~BasicPromiseType() override { m_safeHandle.SetState(CoroutineState::Destroyed); }

    /**
     * @brief Get the return Task object
     *
     * @return ObjectType instance of Task<>/DetachedTask<>
     */
    ObjectType get_return_object() {
        m_safeHandle =
            SafeCoroutineHandle{std::coroutine_handle<BasicPromiseType>::from_promise(*this),
                                typeid(BasicPromiseType), m_source};
        return ObjectType{m_safeHandle};
    }

    auto initial_suspend() noexcept {
        if constexpr (StartPolicy::starts_immediately) {
            (void)m_safeHandle.TryClaimStart();
        }
        return StartPolicy::initial_suspend();
    }

    auto final_suspend() noexcept { return EndPolicy::template final_suspend<BasicPromiseType>(); }

    [[nodiscard]] SafeCoroutineHandle GetSafeHandle() const noexcept { return m_safeHandle; }

    /**
     * @brief Result of a FINISHED wired task, in one place for co_await and Get():
     * rethrows the stored exception, throws OperationCancelledException if cancelled, otherwise
     * gives the value. (Only instantiated for wired tasks; it relies on m_exception, IsCancelled()
     * and value of the policies.)
     */
    ReturnType take_result() {
        if (this->m_exception) {
            std::rethrow_exception(this->m_exception);
        }
        if (this->IsCancelled()) {
            throw OperationCancelledException{};
        }

        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else if constexpr (std::is_reference_v<ReturnType>) {
            if (!this->m_value) {
                throw TaskReturnedNullReferenceException{};
            }
            return *this->m_value;
        } else {
            if (!this->m_value.has_value()) {
                throw TaskFinishedWithoutProducingValueException{};
            }
            return std::move(*this->m_value);
        }
    }
};
} // namespace async_core::exec
