module;

#include <thread>
#include <queue>
#include <concepts>
#include <condition_variable>

export module AsyncCoreLibrary:CoroThread;

import :SafeCoroutineHandle;

using async_core::exec::SafeCoroutineHandle;

export namespace async_core::coro_thread
{
    namespace detail
    {
        template <typename T>
        struct is_std_queue : std::false_type {};

        template <typename T, typename Container>
        struct is_std_queue<std::queue<T, Container>> : std::true_type {};

        template <typename T>
        concept IsStdQueue = is_std_queue<std::remove_cvref_t<T>>::value;

        template <typename MemFn, typename Instance, typename QueueRef, typename... Args>
        concept MemberFuncTakingQueue =
            std::is_member_function_pointer_v<std::decay_t<MemFn>> &&
            IsStdQueue<QueueRef> &&
            std::invocable<MemFn, Instance, QueueRef&, Args...>;
    }

    struct CoroThread
    {
        std::queue<SafeCoroutineHandle> m_tasks;
        std::mutex m_mtx;
        std::condition_variable_any m_cv;
        std::jthread m_thread;

        template <typename Func, typename Instance, typename... Args>
        CoroThread(Func&& f, Instance&& instance, Args&&... args)
            : m_thread(
                std::forward<Func>(f),
                std::forward<Instance>(instance),
                std::ref(*this),
                std::forward<Args>(args)...
            )
        {}

        void push(SafeCoroutineHandle handle) {
            {
                std::lock_guard lock(m_mtx);
                m_tasks.push(std::move(handle));
            }
            m_cv.notify_one();
        }

        ~CoroThread() {
            m_thread.request_stop();
            m_cv.notify_all();
            if (m_thread.joinable()) {
                m_thread.join();
            }
        }
    };
}