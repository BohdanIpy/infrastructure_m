module;

#include <coroutine>
#include <exception>
#include <stop_token>

export module AsyncCoreLibrary:TaskAwaitable;

import :CancellingRoutines;
import :CoroutineStates;
import :Exceptions;
import :SafeCoroutineHandle;
import :Concepts;

namespace async_core::exec {
using common::CoroutineState;
using common::HasGetStopTokenConcept;
using common::HasSafeHandleConcept;
using common::InvalidCoroutineHandleException;
using common::TaskResumeFailedException;
} // namespace async_core::exec

export namespace async_core::exec {
/**
 * @brief Awaiter returned by Task::operator co_await() &&. A short-lived operation object: it does NOT own
 * the frame (the Task does, and it outlives the co_await expression).
 *
 * Requirements on PromiseType:
 *   - GetSafeHandle()                 -> SafeCoroutineHandle
 *   - continuation                    -> std::coroutine_handle<>
 *   - add_parent_stop_token(token)    -> links the child's cancellation node to the awaiting
 * coroutine (must not throw: it runs inside a noexcept await_suspend)
 *   - take_result()                   -> T: rethrows the stored exception, throws
 *                                        OperationCancelledException if cancelled, otherwise
 * returns the value
 *
 * The awaited task must not have been started by anybody else. An already started child is running
 * or parked behind its own awaiter, so returning its handle would resume it twice, and writing
 * `continuation` would race with it. That is reported with TaskResumeFailedException instead of
 * undefined behaviour.
 */
template <typename T, typename PromiseType> struct TaskAwaitable {
    using value_type = T;

    std::coroutine_handle<PromiseType> m_coro;

    explicit TaskAwaitable(std::coroutine_handle<PromiseType> coro) noexcept
        : m_coro(coro) {}

    [[nodiscard]] bool await_ready() const {
        if (!m_coro) {
            return true; 
        }
        if (m_coro.promise().GetSafeHandle().LoadState() != CoroutineState::Created) {
            throw TaskResumeFailedException{};
        }
        return false;
    }

    template <typename CallerPromise>
        requires HasSafeHandleConcept<CallerPromise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<CallerPromise> caller) noexcept {
        auto& promise = m_coro.promise();

        if (!promise.GetSafeHandle().TryClaimStart()) {
            std::terminate();
        }

        promise.continuation = caller;

        if constexpr (HasGetStopTokenConcept<CallerPromise>) {
            promise.add_parent_stop_token(caller.promise().GetStopToken());
        }
        return m_coro;
    }

    value_type await_resume() {
        if (!m_coro) {
            throw InvalidCoroutineHandleException{};
        }
        return m_coro.promise().take_result();
    }
};
} // namespace async_core::exec
