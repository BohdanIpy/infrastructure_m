module;

#include <coroutine>
#include <exception>
#include <memory>
#include <atomic>
#include <optional>
#include <chrono>
#include <mutex>

export module AsyncCoreLibrary:TimeoutAfter;

import :ICoroScheduler;
import :TimerQueue;
import :SwitchTo;

using async_core::coro_loop::ICoroScheduler;
using async_core::coro_loop::SwitchTo;

export namespace async_core::time
{
    enum class RaceResult { PENDING, TASK_WON, TIMED_OUT };

    struct TimeoutException : std::runtime_error {
        TimeoutException() : std::runtime_error("Operation timed out") {}
    };

    template <typename T>
    struct TimeoutSharedState {
        std::mutex mtx;
        bool done{false};
        RaceResult state{RaceResult::PENDING};
        std::coroutine_handle<> caller_handle{nullptr};

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
        bool done{false};
        RaceResult state{RaceResult::PENDING};
        std::coroutine_handle<> caller_handle{nullptr};

        std::exception_ptr exception{nullptr};
        void set_value(void) {

        }

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
        Duration m_duration;
        ICoroScheduler* m_scheduler{nullptr};

        using ResultType = decltype(std::declval<TaskType&>().get());
        using SharedState = TimeoutSharedState<ResultType>;
        std::shared_ptr<SharedState> m_state;

        static DetachedTask RunTaskWrapper(ICoroScheduler* scheduler, TaskType task, std::shared_ptr<SharedState> state) {
            try {
                if constexpr (std::is_void_v<ResultType>) {
                    co_await SwitchTo{scheduler};
                    co_await task;

                    std::coroutine_handle<> to_resume = nullptr;
                    {
                        std::unique_lock lk{state->mtx};
                        if (!state->done) {
                            state->state = RaceResult::TASK_WON;
                            state->done = true;
                            to_resume = state->caller_handle;
                        }
                    }
                    if (to_resume) to_resume.resume();
                } else {
                    auto res = co_await task;

                    std::coroutine_handle<> to_resume = nullptr;
                    {
                        std::unique_lock lk{state->mtx};
                        if (!state->done) {
                            state->result = std::move(res);
                            state->state = RaceResult::TASK_WON;
                            state->done = true;
                            to_resume = state->caller_handle;
                        }
                    }
                    if (to_resume) to_resume.resume();
                }
            } catch (...) {
                std::coroutine_handle<> to_resume = nullptr;
                {
                    std::unique_lock lk{state->mtx};
                    if (!state->done) {
                        state->exception = std::current_exception();
                        state->state = RaceResult::TASK_WON;
                        state->done = true;
                        to_resume = state->caller_handle;
                    }
                }
                if (to_resume) to_resume.resume();
            }
        }

    public:
        // Accept by value to take ownership cleanly
        TimeoutAfter(TimerQueue& queue, ICoroScheduler* scheduler, TaskType task, Duration duration)
            : m_queue(queue), m_task(std::move(task)), m_duration(duration),
              m_state(std::make_shared<SharedState>()), m_scheduler(scheduler) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> caller) {
            m_state->caller_handle = caller;

            m_queue.AddTimer([state = m_state]() {
                std::unique_lock lk{state->mtx};
                if (!state->done)
                {
                    state->state = RaceResult::TIMED_OUT;
                    state->done = true;
                    state->caller_handle.resume();
                }
            }, std::chrono::duration_cast<milliseconds>(m_duration));

            RunTaskWrapper(m_scheduler,std::move(m_task), m_state);
        }

        ResultType await_resume() {
            return m_state->get_value();
        }
    };
}
