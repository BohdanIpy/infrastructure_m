module;

#include <chrono>
#include <coroutine>
#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>
#include <utility>

export module AsyncCoreLibrary:TimerQueue;

import :SafeCoroutineHandle;
import :DetachedTask;

using namespace std::chrono;
using namespace std::chrono_literals;

using async_core::exec::DetachedTask;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::time
{
    using Clock = steady_clock;

    struct TimerEntry {
        SafeCoroutineHandle handle;
        Clock::time_point deadline;

        bool operator>(const TimerEntry& other) const {
            return deadline > other.deadline;
        }
    };

    class TimerQueue
    {
        std::mutex m_mtx;
        std::condition_variable_any m_cv;
        std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> m_timers;
        std::jthread m_worker;

        static DetachedTask LambdaCoroAdapter(const std::function<void()> cb) {
            co_await std::suspend_always{};
            cb();
        }

    public:
        TimerQueue()
        {
            m_worker = std::jthread([this](std::stop_token st)
            {
                while (!st.stop_requested())
                {
                    std::unique_lock lk(m_mtx);

                    if (m_timers.empty())
                    {
                        m_cv.wait(lk, st, [this] { return !m_timers.empty(); });
                    }
                    else
                    {
                        auto deadline = m_timers.top().deadline;
                        m_cv.wait_until(lk, st, deadline, [this, deadline] {
                            return !m_timers.empty() && m_timers.top().deadline < deadline;
                        });
                    }

                    if (st.stop_requested()) break;

                    auto now = Clock::now();
                    while (!m_timers.empty() && m_timers.top().deadline <= now)
                    {
                        auto entry = m_timers.top();
                        m_timers.pop();

                        lk.unlock();
                        if (entry.handle.is_valid()) {
                            entry.handle.try_resume();
                        }
                        lk.lock();
                    }
                }
            });
        }

        void AddTimer(SafeCoroutineHandle handle, const milliseconds timeout)
        {
            std::unique_lock lk(m_mtx);
            m_timers.emplace(handle, Clock::now() + timeout);
            m_cv.notify_one();
        }

        void AddTimer(std::coroutine_handle<> handle, const milliseconds timeout)
        {
            AddTimer(SafeCoroutineHandle{handle, std::make_shared<exec::CoroutineControlBlock>()}, timeout);
        }

        template <typename Callable>
            requires std::invocable<Callable>
        void AddTimer(Callable&& cb, const milliseconds delay) {
            auto task = LambdaCoroAdapter(std::forward<Callable>(cb));
            AddTimer(task.get_safe_handle(), delay);
        }

        ~TimerQueue()
        {
            m_worker.request_stop();
            m_cv.notify_one();
            if (m_worker.joinable())
                m_worker.join();
        }
    };
}
