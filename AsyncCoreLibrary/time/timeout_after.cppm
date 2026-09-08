module;

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <chrono>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <variant>

export module AsyncCoreLibrary:TimeoutAfter;

import :ICoroScheduler;
import :TimerQueue;
import :SwitchTo;
import :DetachedTask;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::coro_loop::SwitchTo;
using async_core::exec::DetachedTask;
using async_core::exec::HasSafeHandle;

export namespace async_core::time
{
    enum class RaceResult { PENDING, TASK_WON, TIMED_OUT };

    struct TimeoutException : std::runtime_error {
        TimeoutException() : std::runtime_error("Operation timed out") {}
    };

    template <typename T>
    struct TimeoutSharedState {
        std::mutex mtx;
        std::atomic<bool> done{false};
        RaceResult state{RaceResult::PENDING};
        std::shared_ptr<SafeCoroutineHandle> caller_handle{nullptr};

        std::optional<T> result;
        std::exception_ptr exception{nullptr};

        void set_value(T&& val) {
            result.emplace(std::move(val));
        }

        T get_value() {
            if (exception) std::rethrow_exception(exception);
            if (result.has_value()) {
                return std::move(*result);
            }
            throw TimeoutException();
        }
    };

    template<>
    struct TimeoutSharedState<void>
    {
        std::mutex mtx;
        std::atomic<bool> done{false};
        RaceResult state{RaceResult::PENDING};
        std::shared_ptr<SafeCoroutineHandle> caller_handle{nullptr};

        std::exception_ptr exception{nullptr};

        void set_value() {}

        void get_value() {
            if (exception) std::rethrow_exception(exception);
            if (this->state == RaceResult::TIMED_OUT)
                throw TimeoutException();
        }
    };

    template <typename TaskType, typename Duration>
    class TimeoutAfter {
        TimerQueue& m_queue;
        TaskType m_task;
        const Duration m_duration;
        ICoroScheduler* m_scheduler{nullptr};

        using ResultType = decltype(std::declval<TaskType&>().get());
        using SharedState = TimeoutSharedState<ResultType>;
        std::shared_ptr<SharedState> m_state;

        static DetachedTask RunTaskWrapper(ICoroScheduler* scheduler, TaskType task, std::shared_ptr<SharedState> state) {
            try {
                if constexpr (std::is_void_v<ResultType> || std::is_same_v<ResultType, std::monostate>) {
                    if (scheduler) {
                        co_await SwitchTo{scheduler};
                    }
                    co_await task;

                    std::shared_ptr<SafeCoroutineHandle> to_resume = nullptr;
                    if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                        std::unique_lock lk{state->mtx};
                        state->state = RaceResult::TASK_WON;
                        to_resume = state->caller_handle;
                    }
                    if (to_resume->is_valid())
                    {
                        to_resume->try_resume();
                    }
                } else {
                    if (scheduler) {
                        co_await SwitchTo{scheduler};
                    }
                    auto res = co_await task;

                    std::shared_ptr<SafeCoroutineHandle> to_resume = nullptr;
                    if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                        std::unique_lock lk{state->mtx};
                        state->result = std::move(res);
                        state->state = RaceResult::TASK_WON;
                        to_resume = state->caller_handle;
                    }
                    if (to_resume->is_valid())
                    {
                        to_resume->try_resume();
                    }
                }
            } catch (...) {
                std::shared_ptr<SafeCoroutineHandle> to_resume = nullptr;
                if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                    std::unique_lock lk{state->mtx};
                    state->exception = std::current_exception();
                    state->state = RaceResult::TASK_WON;
                    to_resume = state->caller_handle;
                }
                if (to_resume->is_valid())
                {
                    to_resume->try_resume();
                }
            }
        }

    public:
        TimeoutAfter(TimerQueue& queue, ICoroScheduler* scheduler, TaskType task, Duration duration)
            : m_queue(queue), m_task(std::move(task)), m_duration(duration),
              m_scheduler(scheduler), m_state(std::make_shared<SharedState>()) {}

        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> caller) {
            m_state->caller_handle = caller.promise().get_safe_handle();

            m_queue.AddTimer([state = m_state]() {
                if (!state->done.exchange(true, std::memory_order_acq_rel)) {
                    std::shared_ptr<SafeCoroutineHandle> to_resume = nullptr;
                    {
                        std::unique_lock lk{state->mtx};
                        state->state = RaceResult::TIMED_OUT;
                        to_resume = state->caller_handle;
                    }
                    if (to_resume->is_valid())
                    {
                        to_resume->try_resume();
                    }
                }
            }, std::chrono::duration_cast<milliseconds>(m_duration));

            RunTaskWrapper(m_scheduler, std::move(m_task), m_state);
        }

        ResultType await_resume() {
            return m_state->get_value();
        }
    };
}