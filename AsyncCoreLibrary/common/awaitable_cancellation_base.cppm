module;

#include <atomic>
#include <concepts>

export module AsyncCoreLibrary:AwaitableCancellationBase;

import :CoroutineStates;

export namespace async_core::common
{
    class AwaitableCancellationBase
    {
    protected:
        std::atomic<AwaitableState> m_state{AwaitableState::Pending};
        void* m_promise_ptr{nullptr};
        void(*m_clear_fn)(void*) noexcept{nullptr};

        template <typename PromiseT>
        void setup_promise_erasure(PromiseT& promise) noexcept
        {
            m_promise_ptr = static_cast<void*>(&promise);
            m_clear_fn = [](void* ptr) noexcept {
                static_cast<PromiseT*>(ptr)->clear_cancellation_callback();
            };
        }

        void cleanup_cancellation_callback() noexcept
        {
            if (m_promise_ptr && m_clear_fn) {
                m_clear_fn(m_promise_ptr);
                m_promise_ptr = nullptr;
                m_clear_fn = nullptr;
            }
        }

        bool try_mark_cancelled() noexcept
        {
            AwaitableState expected = AwaitableState::Pending;
            return m_state.compare_exchange_strong(
                expected, AwaitableState::Cancelled,
                std::memory_order_acq_rel, std::memory_order_relaxed
            );
        }

        bool try_mark_completed() noexcept
        {
            AwaitableState expected = AwaitableState::Pending;
            return m_state.compare_exchange_strong(
                expected, AwaitableState::Completed,
                std::memory_order_acq_rel, std::memory_order_relaxed
            );
        }

    public:
        [[nodiscard]] bool is_cancelled() const noexcept
        {
            return m_state.load(std::memory_order_acquire) == AwaitableState::Cancelled;
        }

        [[nodiscard]] bool consume_result() noexcept
        {
            const bool cancelled = is_cancelled();
            if (!cancelled) {
                cleanup_cancellation_callback();
            }
            return !cancelled; // Returns true if successful, false if cancelled
        }
    };
}