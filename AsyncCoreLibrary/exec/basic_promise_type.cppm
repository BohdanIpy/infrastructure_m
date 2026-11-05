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

using async_core::common::TaskType;

using async_core::policy::LazyStartPolicy;
using async_core::policy::ManualEndPolicy;
using async_core::policy::PromiseReturnBase;
using async_core::policy::ContinuationPolicy;
using async_core::policy::ExceptionPolicy;

using async_core::common::SafeCoroutineHandle;
using async_core::common::CoroutineNode;
using async_core::common::CancellablePromiseMixin;
using async_core::common::IPromiseStopControl;

export namespace async_core::exec
{
    template <
        typename ObjectType,
        typename ReturnType,
        TaskType Kind,
        typename StartPolicy = LazyStartPolicy,
        typename EndPolicy = ManualEndPolicy,
        typename StreamType = std::ostream&,
        StreamType instance = std::cerr
    >
    struct BasicPromiseType :
        PromiseReturnBase<ReturnType>,
        ExceptionPolicy<Kind>,
        ContinuationPolicy<Kind>,
        CoroutineNode,
        CancellablePromiseMixin,
        IPromiseStopControl
    {
        SafeCoroutineHandle safe_handle;

        std::stop_source m_source;
        std::stop_token m_parent_token;

        BasicPromiseType() = default;

        ~BasicPromiseType() override {
            this->remove_from_parent();
            safe_handle.SetState(CoroutineState::Destroyed);
        }

        // --- Cancellation API ---
        [[nodiscard]] std::stop_token get_stop_token() const noexcept {
            return m_source.get_token();
        }

        [[nodiscard]] bool is_cancelled() const noexcept {
            return m_source.stop_requested() || m_parent_token.stop_requested();
        }

        void add_parent_stop_token(const std::stop_token& stop_token) noexcept
        {
            this->m_parent_token = stop_token;
            // register stop callback
            if (this->m_parent_token.stop_possible())
            {
                this->set_cancellation_callback(
                    this->m_parent_token,
                    [](void *ctx) noexcept
                    {
                        auto self = static_cast<BasicPromiseType*>(ctx);
                        self->m_source.request_stop();
                    },
                    this
                );
            }
        }

        void request_stop() noexcept override {
            (void)m_source.request_stop();

            CoroutineNode* child = this->first_child;
            while (child) {
                auto* child_promise = static_cast<BasicPromiseType*>(child);
                child_promise->request_stop();
                child = child->next_sibling;
            }
        }

        // --- Coroutine Lifetime Hooks ---
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

        // Dispatch unhandled exceptions based on TaskType policy
        void unhandled_exception() noexcept {
            if constexpr (Kind == TaskType::Detached) {
                this->handle_exception(instance);
            } else {
                ExceptionPolicy<Kind>::unhandled_exception();
            }
        }
    };
}