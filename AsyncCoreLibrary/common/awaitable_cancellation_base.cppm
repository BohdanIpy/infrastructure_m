module;

#include <concepts>
#include <optional>
#include <stop_token>
#include <utility>

export module AsyncCoreLibrary:AwaitableCancellationBase;

import :Concepts;

export namespace async_core::common {
/**
 * @brief Class, which implements utilities for the cancellation on leaf awaitables
 * @tparam CancelAction Functor or function, which is executed in case of cancellation
 */
template <typename CancelAction>
    requires std::invocable<CancelAction&>
class AwaitableCancellationBase {
    std::optional<std::stop_callback<CancelAction>> m_registration;

  protected:
    /**
     * @brief Function which extracts the stop token from the promise_type.
     *
     * @tparam PromiseT promise_type for getting promise
     * @param promise promise from which extracts stop_token
     * @return std::stop_token
     */
    template <typename PromiseT>
    [[nodiscard]] static std::stop_token stop_token_of(PromiseT& promise) {
        if constexpr (requires { HasGetStopTokenConcept<PromiseT>; }) {
            return promise.GetStopToken();
        } else {
            return {};
        }
    }

    /**
     * @brief Registers a cancellation callback, which is executed when cancellation is requested
     *
     * @param token Source of the cancellation
     * @param action Functor or function for clear
     */
    void watch_stop(std::stop_token token, CancelAction action) {
        if (token.stop_possible()) {
            m_registration.emplace(std::move(token), std::move(action));
        }
    }

    /**
     * @brief removes the cancellation callback, so after executing it wont free resource 2 times
     */
    void unwatch_stop() noexcept { m_registration.reset(); }
};
} // namespace async_core::common
