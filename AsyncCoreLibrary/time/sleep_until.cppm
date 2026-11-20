module;

#include <cassert>
#include <chrono>
#include <coroutine>
#include <stop_token>

export module AsyncCoreLibrary:SleepUntil;

import :TimerQueue;
import :SafeCoroutineHandle;
import :CancellingRoutines;
import :AwaitableCancellationBase;
import :CoroutineStates;
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
    class SleepUntil : public AwaitableCancellationBase<SleepUntil>
    {
        std::reference_wrapper<TimerQueue> m_queue;
        const Clock::time_point m_deadline;
        UUID m_uuid{};

    public:
        SleepUntil(TimerQueue& queue, const Clock::time_point& deadline) noexcept
            : m_queue(queue), m_deadline(deadline)
        {
        }

        bool await_ready() const noexcept
        {
            return Clock::now() >= m_deadline;
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

            const auto now = Clock::now();
            const auto wait_time = (m_deadline > now)
                ? std::chrono::duration_cast<milliseconds>(std::chrono::ceil<milliseconds>(m_deadline - now))
                : milliseconds(0);

            auto [status, real_uuid] = m_queue->AddTimer(safe_handle, wait_time);
            if (status != TimerStatus::Success) {
                m_state.store(AwaitableState::Cancelled, std::memory_order_relaxed);
                return false;
            }

            this->m_uuid = real_uuid;

            if (token.stop_possible()) {
                promise.set_cancellation_callback(
                    token,
                    [](void* ctx) noexcept {
                        auto* self = static_cast<SleepUntil*>(ctx);

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
                        (void)m_queue->CancelTimer(this->m_uuid);
                        return false;
                    }
                }
            }

            return true;
        }

        bool await_resume() noexcept
        {
            // consume_result() already manages cleanup_cancellation_callback()
            return consume_result();
        }
    };
}