module;

#include <coroutine>
#include <exception>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>

export module AsyncCoreLibrary:Task;

import :SafeCoroutineHandle;
import :CancellingRoutines;

import :StartEndPolicy;
import :BasicPromiseType;
import :TaskBase;


using async_core::common::SafeCoroutineHandle;
using async_core::policy::LazyStartPolicy;
using async_core::policy::EagerStartPolicy;
using async_core::common::has_get_stop_token_v;

export namespace async_core::exec
{
    template<typename T, typename Policy = LazyStartPolicy>
    class Task : public TaskBase<Task<T, Policy>, BasicPromiseType<Task<T, Policy>, T, TaskType::Wired, Policy, ManualEndPolicy>>
    {
        using PromiseT = BasicPromiseType<Task<T, Policy>, T, TaskType::Wired, Policy, ManualEndPolicy>;
        using TaskBaseType = TaskBase<Task<T, Policy>, PromiseT>;

    public:
        using value_type = T;
        using ReturnType = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

        using promise_type = BasicPromiseType<
            Task<T, Policy>,
            T,
            TaskType::Wired,
            Policy,
            ManualEndPolicy
        >;

        using Base = TaskBase<Task<T, Policy>, promise_type>;

        explicit Task(SafeCoroutineHandle handle) : Base(std::move(handle)) {}

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept
            : TaskBaseType(std::move(other))
        {
        }

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other) {
                TaskBaseType::operator=(std::move(other));
            }
            return *this;
        }

        ~Task() {
            this->destroy_frame();
        }

        T get()
        {
            if (!this->safe_handle.is_valid()) {
                throw std::runtime_error("Attempting to run an uninitialized task.");
            }

            auto h = this->safe_handle.template raw_handle<promise_type>();

            if (!h.done())
            {
                std::binary_semaphore sem{0};
                h.promise().m_semaphore = &sem;

                if (!this->safe_handle.try_resume()) {
                    h.promise().m_semaphore = nullptr;
                    throw std::runtime_error("Failed to resume task in Task::get().");
                }

                sem.acquire();
            }

            if (h.promise().m_exception) {
                std::rethrow_exception(h.promise().m_exception);
            }

            if (h.promise().is_cancelled()) {
                throw std::runtime_error("Task was cancelled before completion.");
            }

            if constexpr (std::is_void_v<T>) {
                return;
            } else if constexpr (std::is_reference_v<T>) {
                if (!h.promise().value) {
                    throw std::runtime_error("Task returned null reference.");
                }
                return *h.promise().value;
            } else {
                if (!h.promise().value.has_value()) {
                    throw std::runtime_error("Task finished without producing a value.");
                }
                return std::move(*h.promise().value);
            }
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return !this->safe_handle.is_valid() || this->safe_handle.template raw_handle<promise_type>().done();
        }

        template <typename PromiseT>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseT> caller) noexcept
        {
            auto h = this->safe_handle.template raw_handle<promise_type>();
            h.promise().continuation = caller;

            // passing a token
            if constexpr (has_get_stop_token_v<promise_type>)
            {
                h.promise().m_parent_token = caller.promise().get_stop_token();
            }

            if constexpr (std::is_base_of_v<CoroutineNode, PromiseT>) {
                caller.promise().add_child(&h.promise());
            }

            if (h.done()) {
                return caller;
            }

            return h;
        }

        bool is_ready() const noexcept
        {
            if (this->safe_handle.is_valid())
            {
                this->safe_handle.template raw_handle<PromiseT>().done();
            }
            return false;
        }

        T await_resume()
        {
            if (!this->safe_handle.is_valid()) {
                throw std::runtime_error("co_awaiting an invalid or moved-from Task");
            }

            auto h = this->safe_handle.template raw_handle<promise_type>();

            if (h.promise().m_exception) {
                std::rethrow_exception(h.promise().m_exception);
            }

            if (h.promise().is_cancelled()) {
                throw std::runtime_error("Task was cancelled.");
            }

            if constexpr (std::is_void_v<T>) {
                return;
            } else if constexpr (std::is_reference_v<T>) {
                if (!h.promise().value) {
                    throw std::runtime_error("Task returned null reference.");
                }
                return *h.promise().value;
            } else {
                if (!h.promise().value.has_value()) {
                    throw std::runtime_error("Task finished without producing a value.");
                }
                return std::move(*h.promise().value);
            }
        }
    };

    template <typename T>
    using LazyTask = Task<T>;

    template <typename T>
    using EagerTask = Task<T, EagerStartPolicy>;
}
