module;

#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <functional>
#include <new>

export module AsyncCoreLibrary:CoroThread;

import :SafeCoroutineHandle;
import :ScheduleTypes;

namespace async_core::coro_loop {
using common::SafeCoroutineHandle;
using common::ScheduleStatus;
} // namespace async_core::coro_loop

export namespace async_core::coro_loop {
/**
 * @brief CoroThread, main worker thread of the CoroScheduler
 */
struct CoroThread {
    std::queue<SafeCoroutineHandle> m_tasks;
    std::mutex m_mtx;
    std::condition_variable_any m_cv;
    std::jthread m_thread;
    bool m_stopped{false};

    CoroThread() = default;

    template <typename Func> void start(Func&& f) {
        m_thread = std::jthread(std::forward<Func>(f));
    }

    [[nodiscard]]
    ScheduleStatus push(SafeCoroutineHandle handle) {
        if (!handle.IsValid())
            return ScheduleStatus::InvalidHandle;

        {
            std::lock_guard lock(m_mtx);
            if (m_stopped)
                return ScheduleStatus::SchedulerStopped;

            try {
                m_tasks.push(std::move(handle));
            } catch (const std::bad_alloc&) {
                return ScheduleStatus::OutOfMemory;
            }
        }

        m_cv.notify_one();
        return ScheduleStatus::Success;
    }

    void request_stop() {
        {
            std::lock_guard lock(m_mtx);
            m_stopped = true;
            (void)m_thread.request_stop();
        }
        m_cv.notify_all();
    }

    void join() {
        if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id()) {
            m_thread.join();
        }
    }

    CoroThread(const CoroThread&) = delete;
    CoroThread& operator=(const CoroThread&) = delete;
    CoroThread(CoroThread&&) = delete;
    CoroThread& operator=(CoroThread&&) = delete;

    ~CoroThread() {
        request_stop();
        join();
    }
};
} // namespace async_core::coro_loop
