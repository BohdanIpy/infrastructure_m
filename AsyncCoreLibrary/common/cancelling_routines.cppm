module;

#include <coroutine>
#include <stop_token>
#include <utility>

export module AsyncCoreLibrary:CancellingRoutines;

import :Exceptions;
import :Concepts;

namespace async_core::common {
using common::AwaitableDefinedCoAwaitOperatorConcept;
}

export namespace async_core::common {
/**
 * @brief Shielded awaitable must be executed even if the coroutine is cancelled
 * @tparam Awaitable Awaitable which must be shielded - executed even if the cancellation called
 */
template <typename Awaitable> struct Shielded {
    Awaitable&& awaitable;
};

/**
 * @brief Construction of shielded awaitable
 *
 * @tparam Awaitable Type of awaitable to be shielded
 * @param awaitable awaitable
 * @return Shielded<Awaitable> shielded awaitable
 */
template <typename Awaitable>
[[nodiscard]] Shielded<Awaitable> shield(Awaitable&& awaitable) noexcept {
    return Shielded<Awaitable>{std::forward<Awaitable>(awaitable)};
}

/**
 * @brief Awaiter, which ignores the cancellation
 *
 * @tparam Awaitable Type of awaitable, which was shielded
 */
template <typename Awaitable> class ShieldedAwaiter;

/**
 * @brief Implementation of the Cancellation Routines for a promise_type
 */
struct CancellationNode {
    std::stop_source m_source;

    virtual ~CancellationNode() = default;

    /**
     * @brief Get the Stop Token object
     *
     * @return std::stop_token If the coroutine is shielded - return empty stop_token{}, else -
     * valid stop_token
     */
    [[nodiscard]] std::stop_token GetStopToken() const noexcept {
        return m_shielded ? std::stop_token{} : m_source.get_token();
    }

    [[nodiscard]] std::stop_source GetStopSource() const noexcept { return m_source; }
    [[nodiscard]] bool IsStopped() const noexcept { return m_source.stop_requested(); }
    void RequestStop() const noexcept { (void)m_source.request_stop(); }

    /**
     * @brief Function, which cancel awaitables in case of cancellation.
     * Before start of awaitable, co_await is called as `co_await
     * promise_type.await_transform(awaitable)` Here we perform a cancellation check, or can insert
     * some parameters
     * @tparam Awaitable Type of awaitable
     * @param awaitable awaitable instance
     * @return Awaitable&& awaitable after cancellation check
     */
    template <typename Awaitable> Awaitable&& await_transform(Awaitable&& awaitable) {
        if (m_source.stop_requested()) {
            throw OperationCancelledException{};
        }
        return std::forward<Awaitable>(awaitable);
    }

    /**
     * @brief Specialization of await_transform, which ignores the cancellation
     */
    template <typename Awaitable>
    ShieldedAwaiter<Awaitable> await_transform(Shielded<Awaitable>&& shielded) {
        return ShieldedAwaiter<Awaitable>{*this, std::forward<Awaitable>(shielded.awaitable)};
    }

  private:
    template <typename> friend class ShieldedAwaiter;

    bool m_shielded{false};
};

namespace detail {
/**
 * @brief Get the awaiter object:
 * + separate awaitable object(via overloading of co_await operator)
 * + instance itself is awaitable
 *
 * @tparam Awaitable Type of awaitable
 * @param awaitable instance of aaitable
 * @return decltype(auto) return-type
 */
template <typename Awaitable> decltype(auto) get_awaiter(Awaitable&& awaitable) {
    if constexpr (AwaitableDefinedCoAwaitOperatorConcept<Awaitable>) {
        return std::forward<Awaitable>(awaitable).operator co_await();
    } else {
        return std::forward<Awaitable>(awaitable);
    }
}
} // namespace detail

/**
 * @brief Awaitable type, which ignores cancellation
 *
 * @tparam Awaitable Type of awaitable accepted
 */
template <typename Awaitable> class ShieldedAwaiter {
    using Awaiter = decltype(detail::get_awaiter(std::declval<Awaitable>()));

    CancellationNode& m_node;
    Awaiter m_awaiter;

  public:
    ShieldedAwaiter(CancellationNode& node, Awaitable&& awaitable)
        : m_node(node)
        , m_awaiter(detail::get_awaiter(std::forward<Awaitable>(awaitable))) {}

    bool await_ready() { return m_awaiter.await_ready(); }

    /**
     * @brief Specifies the m_shielded for nested calls, use assymetric transfer
     *
     * @tparam PromiseT promise_type of caller
     * @param caller caller coroutine handle
     * @return decltype(auto) return type for general awaitable type
     */
    template <typename PromiseT>
    decltype(auto) await_suspend(std::coroutine_handle<PromiseT> caller) {
        m_node.m_shielded = true;
        try {
            return m_awaiter.await_suspend(caller);
        } catch (...) {
            m_node.m_shielded = false;
            throw;
        }
    }

    /**
     * @brief removes the shielded flag
     *
     * @return decltype(auto) return type for general awaitable type
     */
    decltype(auto) await_resume() {
        m_node.m_shielded = false;
        return m_awaiter.await_resume();
    }
};
} // namespace async_core::common
