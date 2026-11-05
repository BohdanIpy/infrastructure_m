module;

#include <semaphore>
#include <exception>
#include <variant>
#include <coroutine>

export module AsyncCoreLibrary:SyncWait;

import :Task;

using async_core::exec::Task;

export namespace async_core::sync
{
    template <typename TaskType>
        requires requires(TaskType& t) { typename std::remove_cvref_t<TaskType>::promise_type; }
    decltype(auto) sync_wait(TaskType&& task)
    {
        using TaskPlain = std::remove_cvref_t<TaskType>;
        using Promise = typename TaskPlain::promise_type;
        using ValueType = typename TaskPlain::value_type;

        std::binary_semaphore signal{0};

        auto handle = task.get_safe_handle().template raw_handle<Promise>();
        auto& promise = handle.promise();

        promise.continuation = nullptr;
        promise.m_semaphore = &signal;

        handle.resume();

        signal.acquire();

        if (promise.m_exception != nullptr) {
            std::rethrow_exception(promise.m_exception);
        }

        if constexpr (!std::is_void_v<ValueType>) {
            return std::move(promise.value);
        } else {
            return;
        }
    }
}
