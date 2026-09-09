module;

#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <functional>

export module AsyncCoreLibrary:CoroThread;

import :SafeCoroutineHandle;
import :ScheduleTypes;

using async_core::exec::SafeCoroutineHandle;

export namespace async_core::coro_loop
{
    struct CoroThread
    {
        std::queue<std::shared_ptr<SafeCoroutineHandle>> m_tasks;
        std::mutex m_mtx;
        std::condition_variable_any m_cv;
        std::jthread m_thread;
        bool m_stopped{false};

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

        [[nodiscard]]
        ScheduleStatus push(std::shared_ptr<SafeCoroutineHandle> handle) {
            if (handle == nullptr)
                return ScheduleStatus::InvalidHandle;
            {
                std::lock_guard lock(m_mtx);
                if (m_stopped)
                    return ScheduleStatus::SchedulerStopped;
                m_tasks.push(std::move(handle));
            }
            m_cv.notify_one();
            return ScheduleStatus::Success;
        }

        void request_stop() {
            {
                std::lock_guard lock(m_mtx);
                m_stopped = true;
            }
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