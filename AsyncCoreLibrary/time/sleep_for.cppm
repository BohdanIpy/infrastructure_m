module;

#include <cassert>
#include <chrono>
#include <coroutine>
#include <stop_token>

export module AsyncCoreLibrary:SleepFor;

import :TimerQueue;
import :SafeCoroutineHandle;
import :CancellingRoutines;
import :CoroutineStates;
import :AwaitableCancellationBase;
import :Concepts;

namespace async_core::time
{
    using common::HasSafeHandleConcept;
    using common::HasGetStopTokenConcept;
    using common::AwaitableCancellationBase;
    using common::AwaitableState;

    using namespace std::chrono;
    using Clock = steady_clock;
}

export namespace async_core::time
{
    class SleepFor : public AwaitableCancellationBase<SleepFor>
    {
        std::reference_wrapper<TimerQueue> m_queue;
        const milliseconds m_duration;
        UUID m_uuid{};

    public:
        void register_cancellation(std::stop_token st) noexcept
        {

        }

        SleepFor(TimerQueue& queue, const milliseconds duration) noexcept
            : m_queue(queue), m_duration(duration)
        {

        }

        bool await_ready() const noexcept
        {
            return m_duration <= milliseconds(0);
        }

        template <typename PromiseT>
            requires
                HasSafeHandleConcept<PromiseT> and
                HasGetStopTokenConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> h)
        {
            auto& promise = h.promise();

            std::stop_token token = promise.GetStopToken();

            if (token.stop_requested()) {
                m_state.store(AwaitableState::Cancelled, std::memory_order_relaxed);
                return false;
            }

            setup_promise_erasure(promise);

            auto safe_handle = promise.GetSafeHandle();

            auto [status, real_uuid] = m_queue.get().AddTimer(safe_handle, m_duration);
            if (status != TimerStatus::Success) {
                m_state.store(AwaitableState::Cancelled, std::memory_order_relaxed);
                return false;
            }

            this->m_uuid = real_uuid;

            if (token.stop_possible()) {
                promise.set_cancellation_callback(
                    token,
                    [](void* ctx) noexcept {
                        auto* self = static_cast<SleepFor*>(ctx);

                        if (self->try_mark_cancelled()) {
                            auto* prom = static_cast<PromiseT*>(self->m_promise_ptr);
                            (void)prom->GetSafeHandle().TryResume();
                        }
                    },
                    this
                );

                if (token.stop_requested()) {
                    if (try_mark_cancelled()) {
                        promise.clear_cancellation_callback();
                        (void)m_queue.CancelTimer(this->m_uuid);
                        return false;
                    }
                }
            }

            return true;
        }

        bool await_resume() noexcept
        {
            return consume_result();
        }
    };
}