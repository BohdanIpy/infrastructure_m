module;

#include <atomic>
#include <stop_token>

export module AsyncCoreLibrary:AwaitableCancellationBase;

import :CoroutineStates;
import :Concepts;

namespace async_core::common
{
    using common::CancellableAwaiterConcept;
}

export namespace async_core::common
{
    template <typename Derived>
    struct AwaitableCancellationBase
    {
        std::atomic<AwaitableState> m_state{AwaitableState::Pending};
        std::stop_token m_st;

        explicit AwaitableCancellationBase(std::stop_token st) : m_st(std::move(st)) {}

        static_assert(
            CancellableAwaiterConcept<Derived>,
            "Error: Derived awaiter must implement 'void register_cancellation(std::stop_token)'!"
        );

        virtual ~AwaitableCancellationBase() = default;
    };
}
