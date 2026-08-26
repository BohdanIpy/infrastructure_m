module;

#include <semaphore>
#include <exception>

export module AsyncCoreLibrary:SyncWait;

import :Task;

using async_core::exec::Task;

export namespace async_core::sync
{
    template <typename T, typename Policy>
    decltype(auto) sync_wait(Task<T, Policy>&& task)
    {
        std::binary_semaphore signal{0};

        task.m_handle.promise().continuation = nullptr;
        task.m_handle.promise().m_semaphore = &signal;

        task.m_handle.resume();

        signal.acquire();

        if (task.m_handle.promise().m_exception != nullptr)
        {
            std::rethrow_exception(task.m_handle.promise().m_exception);
        }
        if constexpr (!std::is_void_v<T>)
        {
            return std::move(task.m_handle.promise().value);
        }
    }

}