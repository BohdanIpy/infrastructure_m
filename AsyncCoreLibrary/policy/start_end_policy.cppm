module;

#include <coroutine>

export module AsyncCoreLibrary:StartEndPolicy;

import :TaskTypes;
import :CoroutineStates;
import :ContinuationPolicy;
import :SafeCoroutineHandle;
import :CancellingRoutines;

using async_core::common::TaskType;
using async_core::common::CoroutineState;
using async_core::common::HasSafeHandle;
using async_core::common::CoroutineNode;

export namespace async_core::policy
{
    // ==========================================
    // Start & End Suspend Policies
    // ==========================================

    // Concepts for custom user policies

    template<typename PolicyT>
    concept is_start_policy = requires
    {
        { PolicyT::initial_suspend() };
    };

    template<typename PolicyT>
    concept is_end_policy = requires
    {
        { PolicyT::final_suspend() };
    };

    //

    /**
     * Implementation of START policies
     */
    // Suspend never -- always start coroutine and execute until next co_await hit
    struct EagerStartPolicy {
        static auto initial_suspend() noexcept { return std::suspend_never{}; }
    };

    // Suspend after creation. No code is executed
    struct LazyStartPolicy {
        static auto initial_suspend() noexcept { return std::suspend_always{}; }
    };

    /**
     * Implementation of END policies
     */
    /**
     * Implementation of awaitable for asymmetric transfer for Task<>
     */
    template <typename DerivedPromise>
    struct DynamicFinalSuspend
    {
        [[nodiscard]] bool await_ready() const noexcept { return false; }

        /**
         * Child coroutine finished, removed itself from parent, and pass the execution to the parent coroutine
         */
        template <typename Promise>
            requires HasSafeHandle<Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h /* = child handle*/) noexcept {
            auto& promise = h.promise();

            if constexpr (std::is_base_of_v<CoroutineNode, Promise>) {
                promise.remove_from_parent();
            }

            if constexpr (std::is_base_of_v<ContinuationPolicy<TaskType::Wired>, Promise>) {
                if (promise.continuation) {
                    return promise.continuation;
                }
                if (promise.m_semaphore) {
                    promise.m_semaphore->release();
                }
            }

            return std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    /**
     * Policy for Task<>, which requires manual checking if the coroutine is finished or not
     */
    struct ManualEndPolicy {
        template <typename Promise>
        static auto final_suspend() noexcept { return DynamicFinalSuspend<Promise>{}; }
    };

    /**
     * Policy for DetachedTask<>, which does not require precise lifetime management. The coroutine manages it own lifetime
     */
    struct AutoDestroyEndPolicy {
        template <typename Promise>
        static auto final_suspend() noexcept { return std::suspend_never{}; }
    };
}
