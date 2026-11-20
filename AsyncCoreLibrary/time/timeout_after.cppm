module;

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <stdexcept>
#include <cassert>
#include <iostream>

export module AsyncCoreLibrary:TimeoutAfter;

import :ICoroScheduler;
import :TimerQueue;
import :SwitchTo;
import :DetachedTask;
import :SafeCoroutineHandle;
import :Exceptions;
import :Concepts;

import CoreUtility;

namespace async_core::time
{
    using coro_loop::ICoroScheduler;
    using coro_loop::SwitchTo;
    using exec::DetachedTask;
    using common::HasSafeHandleConcept;
    using common::SafeCoroutineHandle;
    using common::OperationCancelledException;
    using common::TimeoutException;
    using common::HasGetStopTokenConcept;
    using common::HasRequestStopConcept;
    using core_utility::uuid::UUID;
}

export namespace async_core::time
{
    enum class RaceResult { PENDING, TASK_WON, TIMED_OUT, CANCELLED };

    template <typename T>
    struct BaseSetValueFunction {
        std::optional<T> result;

        template <typename U>
            requires std::convertible_to<U, T>
        void set_value(U&& val) {
            result.emplace(std::forward<U>(val));
        }

        T extract_result() {
            return std::move(*result);
        }

        bool has_result() const noexcept {
            return result.has_value();
        }
    };

    template <typename T>
    struct BaseSetValueFunction<T&> {
        T* result{nullptr};

        void set_value(T& val) noexcept {
            result = std::addressof(val);
        }

        T& extract_result() const {
            return *result;
        }

        bool has_result() const noexcept {
            return result != nullptr;
        }
    };

    template <>
    struct BaseSetValueFunction<void> {
        void set_value() noexcept {}
        void extract_result() const noexcept {}
        bool has_result() const noexcept { return true; }
    };

    template <typename T>
    struct TimeoutSharedState : BaseSetValueFunction<T> {
        std::mutex mtx;
        std::atomic<bool> done{false};
        std::atomic<bool> cancelled{false};
        RaceResult state{RaceResult::PENDING};
        SafeCoroutineHandle caller_handle;

        UUID timer_id{};

        std::exception_ptr exception{nullptr};

        using BaseSetValueFunction<T>::set_value;

        T get_value() {
            if (exception) {
                std::rethrow_exception(exception);
            }
            if (state == RaceResult::CANCELLED) {
                throw OperationCancelledException();
            }
            if (state == RaceResult::TIMED_OUT) {
                throw TimeoutException();
            }

            if constexpr (!std::is_void_v<T>) {
                if (this->has_result()) {
                    return this->extract_result();
                }
                throw TimeoutException();
            } else {
                return;
            }
        }
    };

    template <typename TaskType, typename Duration>
    class TimeoutAfter {
        TimerQueue& m_queue;
        TaskType m_task;
        const Duration m_duration;
        ICoroScheduler& m_scheduler;

        using ResultType = decltype(std::declval<TaskType&>().get());
        using SharedState = TimeoutSharedState<ResultType>;
        std::shared_ptr<SharedState> m_state;

        SafeCoroutineHandle m_innerSafeHandle;
        SafeCoroutineHandle m_wrapperHandle;

        static DetachedTask RunTaskWrapper(ICoroScheduler& scheduler, TaskType& task, std::shared_ptr<SharedState> state, TimerQueue& queue) {
            try {
                auto _ = co_await SwitchTo{scheduler};

                if (state->cancelled.load(std::memory_order_relaxed)) {
                    co_return;
                }

                if constexpr (std::is_void_v<ResultType>) {
                    co_await task;

                    if (state->cancelled.load(std::memory_order_relaxed)) {
                        co_return;
                    }

                    if (!state->done.exchange(true, std::memory_order_acq_rel))
                    {
                        {
                            std::unique_lock lk{state->mtx};
                            queue.CancelTimer(state->timer_id);
                        }

                        SafeCoroutineHandle to_resume{};
                        {
                            std::unique_lock lk{state->mtx};
                            state->state = RaceResult::TASK_WON;
                            to_resume = state->caller_handle;
                        }
                        if (to_resume.IsValid()) to_resume.TryResume();
                    }
                } else {
                    auto res = co_await task;

                    if (state->cancelled.load(std::memory_order_relaxed)) {
                        co_return;
                    }

                    if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                        {
                            std::unique_lock lk{state->mtx};
                            queue.CancelTimer(state->timer_id);
                        }

                        SafeCoroutineHandle to_resume{};
                        {
                            std::unique_lock lk{state->mtx};
                            state->set_value(std::move(res));
                            state->state = RaceResult::TASK_WON;
                            to_resume = state->caller_handle;
                        }
                        if (to_resume.IsValid()) to_resume.TryResume();
                    }
                }
            } catch (...) {
                if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                    {
                        std::unique_lock lk{state->mtx};
                        queue.CancelTimer(state->timer_id);
                    }

                    SafeCoroutineHandle to_resume{};
                    {
                        std::unique_lock lk{state->mtx};
                        state->exception = std::current_exception();
                        state->state = RaceResult::TASK_WON;
                        to_resume = state->caller_handle;
                    }
                    if (to_resume.IsValid()) to_resume.TryResume();
                }
            }
        }

    public:
        TimeoutAfter(TimerQueue& queue, ICoroScheduler& scheduler, TaskType task, Duration duration)
            : m_queue(queue), m_task(std::move(task)), m_duration(duration),
              m_scheduler(scheduler), m_state(std::make_shared<SharedState>()) {}

        TimeoutAfter(const TimeoutAfter&) = delete;
        TimeoutAfter& operator=(const TimeoutAfter&) = delete;
        TimeoutAfter(TimeoutAfter&&) noexcept = default;
        TimeoutAfter& operator=(TimeoutAfter&&) noexcept = default;

        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> caller) {
            {
                std::unique_lock lk{m_state->mtx};
                m_state->caller_handle = caller.promise().GetSafeHandle();
            }

            m_innerSafeHandle = m_task.GetSafeHandle();

            // Check if caller promise provides a stop token
            if constexpr (HasGetStopTokenConcept<PromiseT>) {
                auto stop_token = caller.promise().GetStopToken();
                if (stop_token.stop_requested()) {
                    m_state->done.store(true, std::memory_order_relaxed);
                    m_state->cancelled.store(true, std::memory_order_relaxed);
                    m_state->state = RaceResult::CANCELLED;
                    return;
                }
            }

            m_wrapperHandle = RunTaskWrapper(m_scheduler, m_task, m_state, m_queue).GetSafeHandle();

            auto [status, uuid] = m_queue.AddTimer([weak_state = std::weak_ptr<SharedState>(m_state), task_safe = m_innerSafeHandle]() mutable {
                if (auto state = weak_state.lock()) {
                    if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                        state->cancelled.store(true, std::memory_order_relaxed);

                        if constexpr (HasRequestStopConcept<SafeCoroutineHandle>) {
                            task_safe.RequestStop();
                        }

                        SafeCoroutineHandle to_resume{};
                        {
                            std::unique_lock lk{state->mtx};
                            state->state = RaceResult::TIMED_OUT;
                            to_resume = state->caller_handle;
                        }
                        if (to_resume.IsValid()) {
                            (void)to_resume.TryResume();
                        }
                    }
                }
            }, std::chrono::duration_cast<std::chrono::milliseconds>(m_duration));

            if (status != TimerStatus::Success) [[unlikely]]
            {
                m_state->done.store(true, std::memory_order_relaxed);
                if constexpr (HasRequestStopConcept<SafeCoroutineHandle>) {
                    m_innerSafeHandle.RequestStop();
                }
                {
                    std::unique_lock lk{m_state->mtx};
                    m_state->exception = std::make_exception_ptr(
                        std::runtime_error("Failed to schedule timer in TimerQueue")
                    );
                    m_state->state = RaceResult::TASK_WON;
                }
                return;
            }
            {
                std::unique_lock lk{m_state->mtx};
                this->m_state->timer_id = uuid;
            }
        }

        void request_stop() noexcept {
            if (!m_state->done.exchange(true, std::memory_order_acq_rel)) {
                m_state->cancelled.store(true, std::memory_order_relaxed);

                m_queue.CancelTimer(m_state->timer_id);

                if constexpr (HasRequestStopConcept<TaskType>) {
                    m_task.RequestStop();
                } else if constexpr (HasRequestStopConcept<SafeCoroutineHandle>) {
                    m_innerSafeHandle.RequestStop();
                }

                SafeCoroutineHandle to_resume{};
                {
                    std::unique_lock lk{m_state->mtx};
                    m_state->state = RaceResult::CANCELLED;
                    to_resume = m_state->caller_handle;
                }
                if (to_resume.IsValid()) {
                    to_resume.TryResume();
                }
            }
        }

        ResultType await_resume() {
            try {
                return m_state->get_value();
            } catch (...) {
                (void)m_innerSafeHandle.TryDestroyUnstarted();
                (void)m_wrapperHandle.TryDestroyUnstarted();
                throw;
            }
        }
    };

}
