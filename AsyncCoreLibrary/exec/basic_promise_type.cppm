module;

#include <iostream>
#include <stop_token>
#include <coroutine>
#include <exception>

export module AsyncCoreLibrary:BasicPromiseType;

import :SafeCoroutineHandle;

import :TaskTypes;

import :StartEndPolicy;
import :ReturnValuePolicy;
import :ExceptionHandlingPolicy;
import :ContinuationPolicy;
import :CancellingRoutines;
import :CoroutineStates;

namespace async_core::exec
{
    using common::TaskType;
    using policy::LazyStartPolicy;
    using policy::ManualEndPolicy;
    using policy::PromiseReturnBase;
    using policy::ContinuationPolicy;
    using policy::ExceptionPolicy;
    using common::SafeCoroutineHandle;
    using common::CancellationNode;
    using common::CoroutineState;
}

export namespace async_core::exec
{
    template <
        typename ObjectType,
        typename ReturnType,
        TaskType Kind,
        typename StartPolicy = LazyStartPolicy,
        typename EndPolicy = ManualEndPolicy
    >
    struct BasicPromiseType :
        PromiseReturnBase<ReturnType>,
        ExceptionPolicy<Kind>,
        ContinuationPolicy<Kind>,
        CancellationNode
    {

        SafeCoroutineHandle safe_handle;

        BasicPromiseType() = default;

        ~BasicPromiseType() override {
            this->RemoveFromParent();
            safe_handle.SetState(CoroutineState::Destroyed);
        }

        ObjectType get_return_object() {
            safe_handle = SafeCoroutineHandle{
                std::coroutine_handle<BasicPromiseType>::from_promise(*this),
                typeid(BasicPromiseType)
            };
            return ObjectType{safe_handle};
        }

        auto initial_suspend() noexcept {
            return StartPolicy::initial_suspend();
        }

        auto final_suspend() noexcept {
            return EndPolicy::template final_suspend<BasicPromiseType>();
        }

        [[nodiscard]] SafeCoroutineHandle get_safe_handle() const noexcept {
            return safe_handle;
        }
    };
}
