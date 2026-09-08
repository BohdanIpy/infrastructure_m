module;

#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <functional>

export module AsyncCoreLibrary:CoroThread;

import :SafeCoroutineHandle;

using async_core::exec::SafeCoroutineHandle;

export namespace async_core::coro_thread
{
    struct CoroThread
    {
        std::queue<std::shared_ptr<SafeCoroutineHandle>> m_tasks;
        std::mutex m_mtx;
        std::condition_variable_any m_cv;
        std::jthread m_thread;

        CoroThread() = default;

        template <typename Func, typename Instance, typename... Args>
        void start(Func&& f, Instance* instance, Args&&... args) {
            m_thread = std::jthread(
                std::forward<Func>(f),
                instance,
                std::ref(*this),
                std::forward<Args>(args)...
            );
        }

        void push(std::shared_ptr<SafeCoroutineHandle> handle) {
            {
                std::lock_guard lock(m_mtx);
                m_tasks.push(std::move(handle));
            }
            m_cv.notify_one();
        }

        void request_stop() {
            m_thread.request_stop();
            m_cv.notify_all();
        }

        CoroThread(const CoroThread&) = delete;
        CoroThread& operator=(const CoroThread&) = delete;
        CoroThread(CoroThread&&) = delete;
        CoroThread& operator=(CoroThread&&) = delete;

        ~CoroThread() {
            request_stop();
        }
    };
}