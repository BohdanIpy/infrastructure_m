module;

#include <concepts>
#include <coroutine>
#include <type_traits>

export module AsyncCoreLibrary:StartEndPolicy;

import :TaskTypes;
import :CoroutineStates;
import :ContinuationPolicy;
import :SafeCoroutineHandle;
import :CancellingRoutines;
import :Concepts;

namespace async_core::policy {
using common::CancellationNode;
using common::CoroutineState;
using common::HasSafeHandleConcept;
using common::TaskType;
} // namespace async_core::policy

export namespace async_core::policy {
// ==========================================
// Start & End Suspend Policies
// ==========================================

template <typename PolicyT>
concept IsStartPolicyConcept = requires {
    { PolicyT::initial_suspend() };
    { PolicyT::starts_immediately } -> std::convertible_to<bool>;
};

/// An end policy provides final_suspend<Promise>().
namespace detail {
struct EndPolicyProbe {};
} // namespace detail

template <typename PolicyT>
concept IsEndPolicyConcept = requires {
    { PolicyT::template final_suspend<detail::EndPolicyProbe>() };
};

/**
 * START policies
 */
// The body starts running at once, until the first co_await that really suspends.
// Only for DetachedTask: nobody can await it later (a running coroutine must never be resumed a
// second time).
struct EagerStartPolicy {
    static constexpr bool starts_immediately = true;
    static auto initial_suspend() noexcept { return std::suspend_never{}; }
};

// Suspended after creation, no code is executed until somebody starts it.
struct LazyStartPolicy {
    static constexpr bool starts_immediately = false;
    static auto initial_suspend() noexcept { return std::suspend_always{}; }
};

/**
 * END policies
 */
/**
 * @brief Final awaiter of Task<>: hands control to whoever waits for it: the awaiting coroutine
 * (symmetric transfer), or a thread blocked in Task::Get().
 */
template <typename DerivedPromise> struct DynamicFinalSuspend {
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    /**
     * @brief The key point of this function is to be executed in the end of Task<>, so we have to
     * perform a symmetric transfer
     *
     * @tparam Promise
     */
    template <typename Promise>
        requires HasSafeHandleConcept<Promise>
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<Promise> h /* = finished coroutine*/) noexcept {
        auto& promise = h.promise();

        if constexpr (std::is_base_of_v<ContinuationPolicy<TaskType::Wired>, Promise>) {
            if (auto next = promise.m_continuation) {
                return next;
            }
            if (auto* sem = promise.m_semaphore) {
                sem->release();
            }
        }

        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

/**
 * @brief Policy for Task<>: the frame stays alive at the final suspend point and is destroyed by
 * the owning Task.
 */
struct ManualEndPolicy {
    template <typename Promise> static auto final_suspend() noexcept {
        return DynamicFinalSuspend<Promise>{};
    }
};

/**
 * @brief Policy for DetachedTask<>: the coroutine destroys its own frame when it finishes.
 */
struct AutoDestroyEndPolicy {
    template <typename Promise> static auto final_suspend() noexcept {
        return std::suspend_never{};
    }
};
} // namespace async_core::policy
