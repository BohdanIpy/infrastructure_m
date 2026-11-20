module;

#include <cassert>
#include <coroutine>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <utility>

export module AsyncCoreLibrary:AsyncAutoResetEvent;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :CoroutineStates;
import :Concepts;

namespace async_core::sync
{
    using coro_loop::ICoroScheduler;
    using common::ScheduleStatus;
    using common::SafeCoroutineHandle;
    using common::HasSafeHandleConcept;
    using common::AwaitableCancellationBase;
    using common::AwaitableState;
}

export namespace async_core::sync
{
    class AsyncAutoResetEvent
    {
    public:
        struct Awaiter;
    private:
        ICoroScheduler* m_scheduler;
        std::mutex m_mtx;

        struct WaiterNode {
            Awaiter* awaiter;
            SafeCoroutineHandle safe_handle;
        };
        std::queue<WaiterNode> m_waiters;
        bool m_signaled{false};

    public:
        struct Awaiter : public AwaitableCancellationBase<Awaiter>
        {
            friend class AsyncAutoResetEvent;

            AsyncAutoResetEvent& event;
            SafeCoroutineHandle safe_handle{};

            explicit Awaiter(AsyncAutoResetEvent& evt) noexcept : AwaitableCancellationBase<Awaiter>(std::stop_token{}), event(evt)
            {

            }

            void register_cancellation(std::stop_token)
            {

            }

            bool await_ready() noexcept
            {
                std::lock_guard lock(event.m_mtx);
                if (event.m_signaled)
                {
                    event.m_signaled = false;
                    return true;
                }
                return false;
            }

            template <typename PromiseT>
                requires HasSafeHandleConcept<PromiseT>
            bool await_suspend(std::coroutine_handle<PromiseT> h) noexcept
            {
                std::unique_lock lock(event.m_mtx);

                if (event.m_signaled)
                {
                    event.m_signaled = false;
                    return false;
                }

                auto& promise = h.promise();
                std::stop_token token = promise.GetStopToken();

                if (token.stop_requested()) {
                    m_state.store(AwaitableState::Cancelled, std::memory_order_relaxed);
                    return false;
                }

                setup_promise_erasure(promise);
                this->safe_handle = promise.GetSafeHandle();

                if (token.stop_possible()) {
                    promise.set_cancellation_callback(
                        token,
                        [](void* ctx) noexcept {
                            auto* self = static_cast<Awaiter*>(ctx);
                            if (self->try_mark_cancelled()) {
                                bool resume_now = false;
                                {
                                    std::lock_guard lock(self->event.m_mtx);
                                    resume_now = true;
                                }

                                if (resume_now && self->safe_handle.IsValid()) {
                                    (void)self->safe_handle.TryResume();
                                }
                            }
                        },
                        this
                    );

                    if (token.stop_requested()) {
                        if (try_mark_cancelled()) {
                            promise.clear_cancellation_callback();
                            return false;
                        }
                    }
                }

                event.m_waiters.push(WaiterNode{ this, this->safe_handle });
                return true;
            }

            bool await_resume() noexcept
            {
                return consume_result();
            }
        };

        explicit AsyncAutoResetEvent(ICoroScheduler* sched, bool initially_signaled = false) noexcept
            : m_scheduler(sched), m_signaled(initially_signaled)
        {
            assert(sched);
        }

        AsyncAutoResetEvent(const AsyncAutoResetEvent&) = delete;
        AsyncAutoResetEvent& operator=(const AsyncAutoResetEvent&) = delete;
        AsyncAutoResetEvent(AsyncAutoResetEvent&&) noexcept = delete;
        AsyncAutoResetEvent& operator=(AsyncAutoResetEvent&&) noexcept = delete;

        void set() noexcept
        {
            while (true)
            {
                WaiterNode node{};
                {
                    std::unique_lock lock(m_mtx);
                    while (!m_waiters.empty())
                    {
                        node = m_waiters.front();
                        m_waiters.pop();

                        if (node.awaiter && node.awaiter->try_mark_completed())
                        {
                            break;
                        }
                        node = {};
                    }

                    if (!node.safe_handle.IsValid())
                    {
                        m_signaled = true;
                        return;
                    }
                }

                const auto status = m_scheduler->schedule(std::move(node.safe_handle));

                if (status == ScheduleStatus::Success)
                {
                    return;
                }

                if (status == ScheduleStatus::InvalidHandle)
                {
                    continue;
                }

                if (status == ScheduleStatus::SchedulerStopped)
                {
                    std::lock_guard lock(m_mtx);
                    m_signaled = true;
                    return;
                }

                return;
            }
        }

        Awaiter operator co_await() noexcept
        {
            return Awaiter{*this};
        }
    };
}