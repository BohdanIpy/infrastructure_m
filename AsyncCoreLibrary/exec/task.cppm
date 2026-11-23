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
import :TaskAwaitable;
import :Exceptions;
import :TaskTypes;
import :Concepts;

namespace async_core::exec {
using common::HasGetStopTokenConcept;
using common::InvalidCoroutineHandleException;
using common::SafeCoroutineHandle;
using common::TaskResumeFailedException;
using common::WiredTaskES;
using exec::TaskCancellable;
using policy::LazyStartPolicy;
} // namespace async_core::exec

export namespace async_core::exec {
/// A wired Task is always lazy (BasicPromiseType static_asserts it): it starts when it is awaited,
/// or by Get()/TryStart().
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

    using Base = TaskBase;
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

    /**
     * Awaits the task from another coroutine:  auto v = co_await std::move(task);
     * The Task keeps owning the frame; the awaiter only runs it. Rvalue-only on purpose.
     */
    [[nodiscard]] TaskAwaitable<T, promise_type> operator co_await() && noexcept {
        if (!this->m_safeHandle.IsValid()) {
            return TaskAwaitable<T, promise_type>{std::coroutine_handle<promise_type>{}};
        }
        return TaskAwaitable<T, promise_type>{
            this->m_safeHandle.template RawHandle<promise_type>()};
    }

    /**
     * @brief Runs the task and BLOCKS the calling thread until it finishes (rethrows its exception,
     * throws OperationCancelledException if it was cancelled). For the main thread and tests only:
     * calling it on a scheduler worker can deadlock if the task needs that worker. The value is
     * moved out, so call it once.
     */
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

        return h.promise().take_result();
    }

    [[nodiscard]] bool IsReady() const noexcept {
        return this->m_safeHandle.IsValid() &&
               this->m_safeHandle.template RawHandle<promise_type>().done();
    }
};

template <typename T> using LazyTask = Task<T>;
} // namespace async_core::exec
