module;

#include <coroutine>
#include <exception>
#include <semaphore>
#include <type_traits>
#include <utility>
#include <variant>

export module AsyncCoreLibrary:Task;

import :SafeCoroutineHandle;
import :CancellingRoutines;
import :StartEndPolicy;
import :BasicPromiseType;
import :TaskBase;
import :TaskCancellable;
import :Exceptions;
import :TaskTypes;
import :Concepts;

namespace async_core::exec {
using common::HasGetStopTokenConcept;
using common::InvalidCoroutineHandleException;
using common::OperationCancelledException;
using common::SafeCoroutineHandle;
using common::TaskFinishedWithoutProducingValueException;
using common::TaskResumeFailedException;
using common::TaskReturnedNullReferenceException;
using common::WiredTaskES;
using exec::TaskCancellable;
using policy::EagerStartPolicy;
using policy::LazyStartPolicy;
} // namespace async_core::exec

export namespace async_core::exec {
template <typename T, typename Policy = LazyStartPolicy>
class Task
    : public TaskBase<WiredTaskES>
    , public TaskCancellable<Task<T, Policy>> {
  public:
    using value_type = T;
    /**
     * This alias is used for some functions, which requires to hold some non-void type
     */
    using return_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    /**
     * @brief promise_type definition for c++ compiler
     */
    using promise_type =
        BasicPromiseType<Task, value_type, TaskType::Wired, Policy, ManualEndPolicy>;

    /**
     * base task type
     */
    using Base = TaskBase;

    /**
     * alias for exposing the task_type(wired/detached)
     */
    using Base::task_type;

    explicit Task(SafeCoroutineHandle handle)
        : Base(std::move(handle)) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : Base(std::move(other)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            Base::operator=(std::move(other));
        }
        return *this;
    }

    value_type Get() {
        if (!this->m_safeHandle.IsValid()) {
            throw InvalidCoroutineHandleException{};
        }

        auto h = this->m_safeHandle.template RawHandle<promise_type>();

        if (!h.done()) {
            std::binary_semaphore sem{0};
            h.promise().m_semaphore = &sem;

            if (!this->m_safeHandle.TryResume()) {
                h.promise().m_semaphore = nullptr;
                throw TaskResumeFailedException{};
            }

            sem.acquire();
        }

        if (h.promise().m_exception) {
            std::rethrow_exception(h.promise().m_exception);
        }

        if (h.promise().IsCancelled()) {
            throw OperationCancelledException{};
        }

        if constexpr (std::is_void_v<T>) {
            return;
        } else if constexpr (std::is_reference_v<T>) {
            if (!h.promise().value) {
                throw TaskReturnedNullReferenceException{};
            }
            return *h.promise().value;
        } else {
            if (!h.promise().value.has_value()) {
                throw TaskFinishedWithoutProducingValueException{};
            }
            return std::move(*h.promise().value);
        }
    }

    [[nodiscard]] bool IsReady() const noexcept {
        if (this->m_safeHandle.IsValid()) {
            this->m_safeHandle.template RawHandle<promise_type>().done();
        }
        return false;
    }
};

template <typename T> using LazyTask = Task<T>;

template <typename T> using EagerTask = Task<T, EagerStartPolicy>;
} // namespace async_core::exec
