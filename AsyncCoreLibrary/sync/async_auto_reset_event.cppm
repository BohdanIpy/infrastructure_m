// module;
//
// #include <cassert>
// #include <coroutine>
// #include <memory>
// #include <mutex>
// #include <queue>
// #include <utility>
//
// export module AsyncCoreLibrary:AsyncAutoResetEvent;
//
// import :ICoroScheduler;
// import :SafeCoroutineHandle;
// import :AwaitableCancellationBase;
//
// using async_core::coro_loop::ICoroScheduler;
// using async_core::common::ScheduleStatus;
// using async_core::common::SafeCoroutineHandle;
// using async_core::common::HasSafeHandle;
// using async_core::common::AwaitableCancellationBase;
// using async_core::common::AwaitableState;
//
// export namespace async_core::sync
// {
//     class AsyncAutoResetEvent
//     {
//         ICoroScheduler* m_scheduler;
//
//         std::mutex m_mtx;
//         std::queue<SafeCoroutineHandle> m_handles;
//         bool m_signaled{false};
//
//     public:
//         struct Awaiter : AwaitableCancellationBase
//         {
//             AsyncAutoResetEvent& event;
//
//             bool await_ready() noexcept
//             {
//                 std::lock_guard lock(event.m_mtx);
//                 if (event.m_signaled && event.m_handles.empty())
//                 {
//                     event.m_signaled = false;
//                     return true;
//                 }
//                 return false;
//             }
//
//             template <typename PromiseT>
//                 requires HasSafeHandle<PromiseT>
//             bool await_suspend(std::coroutine_handle<PromiseT> h) noexcept
//             {
//                 std::unique_lock lock(event.m_mtx);
//                 auto& promise = h.promise();
//                 if (event.m_signaled && event.m_handles.empty())
//                 {
//                     event.m_signaled = false;
//                     return false;
//                 }
//
//                 std::stop_token token = h.get_stop_token();
//
//                 if (token.stop_requested()) {
//                     m_state.store(AwaitableState::Cancelled, std::memory_order_relaxed);
//                     return false;
//                 }
//
//                 setup_promise_erasure(promise);
//
//                 if (token.stop_possible()) {
//                     promise.set_cancellation_callback(
//                         token,
//                         [](void* ctx) noexcept {
//                             auto* self = static_cast<Awaiter*>(ctx);
//
//                             if (self->try_mark_cancelled()) {
//                                 auto* prom = static_cast<PromiseT*>(self->m_promise_ptr);
//                                 (void)prom->get_safe_handle().try_resume();
//                             }
//                         },
//                         this
//                     );
//
//                     if (token.stop_requested()) {
//                         if (try_mark_cancelled()) {
//                             promise.clear_cancellation_callback();
//                             return false;
//                         }
//                     }
//                 }
//
//                 event.m_handles.push(h.promise().get_safe_handle());
//                 return true;
//             }
//
//             void await_resume() const noexcept
//             {
//
//             }
//         };
//
//         explicit AsyncAutoResetEvent(ICoroScheduler* sched, bool initially_signaled = false) noexcept
//             : m_scheduler(sched), m_signaled(initially_signaled)
//         {
//             assert(sched);
//         }
//
//         AsyncAutoResetEvent(const AsyncAutoResetEvent&) = delete;
//         AsyncAutoResetEvent& operator=(const AsyncAutoResetEvent&) = delete;
//         AsyncAutoResetEvent(AsyncAutoResetEvent&&) noexcept = delete;
//         AsyncAutoResetEvent& operator=(AsyncAutoResetEvent&&) noexcept = delete;
//
//         void set() noexcept
//         {
//             while (true)
//             {
//                 SafeCoroutineHandle safe_h{};
//                 {
//                     std::unique_lock lock(m_mtx);
//                     while (!m_handles.empty())
//                     {
//                         safe_h = std::move(m_handles.front());
//                         m_handles.pop();
//
//                         if (safe_h.is_valid())
//                         {
//                             break;
//                         }
//                     }
//                     if (!safe_h.is_valid())
//                     {
//                         m_signaled = true;
//                         return;
//                     }
//                 }
//
//                 const auto status = m_scheduler->schedule(std::move(safe_h));
//
//                 if (status == ScheduleStatus::Success)
//                 {
//                     return;
//                 }
//
//                 if (status == ScheduleStatus::InvalidHandle)
//                 {
//                     continue;
//                 }
//
//                 if (status == ScheduleStatus::SchedulerStopped)
//                 {
//                     std::lock_guard lock(m_mtx);
//                     m_signaled = true;
//                     return;
//                 }
//
//                 return;
//             }
//         }
//
//         Awaiter operator co_await() noexcept
//         {
//             return Awaiter{*this};
//         }
//     };
// }
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

using async_core::coro_loop::ICoroScheduler;
using async_core::common::ScheduleStatus;
using async_core::common::SafeCoroutineHandle;
using async_core::common::HasSafeHandle;
using async_core::common::AwaitableCancellationBase;
using async_core::common::AwaitableState;

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
        struct Awaiter : public AwaitableCancellationBase
        {
            friend class AsyncAutoResetEvent;

            AsyncAutoResetEvent& event;
            SafeCoroutineHandle safe_handle{};

            explicit Awaiter(AsyncAutoResetEvent& evt) noexcept : event(evt) {}

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
                requires HasSafeHandle<PromiseT>
            bool await_suspend(std::coroutine_handle<PromiseT> h) noexcept
            {
                std::unique_lock lock(event.m_mtx);

                if (event.m_signaled)
                {
                    event.m_signaled = false;
                    return false;
                }

                auto& promise = h.promise();
                std::stop_token token = promise.get_stop_token();

                if (token.stop_requested()) {
                    m_state.store(AwaitableState::Cancelled, std::memory_order_relaxed);
                    return false;
                }

                setup_promise_erasure(promise);
                this->safe_handle = promise.get_safe_handle();

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

                                if (resume_now && self->safe_handle.is_valid()) {
                                    (void)self->safe_handle.try_resume();
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

                    if (!node.safe_handle.is_valid())
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