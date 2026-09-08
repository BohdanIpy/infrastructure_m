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
    template <typename T, typename Policy>
    decltype(auto) sync_wait(Task<T, Policy>&& task)
    {
        std::binary_semaphore signal{0};

        task.get_safe_handle()->template raw_handle<typename Task<T, Policy>::promise_type>().promise().continuation = nullptr;
        task.get_safe_handle()->template raw_handle<typename Task<T, Policy>::promise_type>().promise().m_semaphore = &signal;

        task.get_safe_handle()->template raw_handle<typename Task<T, Policy>::promise_type>().resume();

        signal.acquire();

        if (task.get_safe_handle()->template raw_handle<typename Task<T, Policy>::promise_type>().promise().m_exception != nullptr)
        {
            std::rethrow_exception(task.get_safe_handle()->template raw_handle<typename Task<T, Policy>::promise_type>().promise().m_exception);
        }
        if constexpr (!std::is_void_v<T>)
        {
            return std::move(task.get_safe_handle()->template raw_handle<typename Task<T, Policy>::promise_type>().promise().value);
        }
        else
        {
            return std::monostate{};
        }
    }
}
