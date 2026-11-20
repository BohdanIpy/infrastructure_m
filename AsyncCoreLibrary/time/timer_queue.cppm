module;

#include <chrono>
#include <coroutine>
#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>
#include <utility>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>

export module AsyncCoreLibrary:TimerQueue;

import CoreUtility;

import :SafeCoroutineHandle;
import :DetachedTask;

namespace async_core::time
{
    using namespace std::chrono;
    using namespace std::chrono_literals;
    using Clock = steady_clock;
    using exec::DetachedTask;
    using common::SafeCoroutineHandle;
    using common::HasSafeHandleConcept;
    using core_utility::uuid::UUID;
}

export namespace async_core::time
{

    enum class TimerStatus {
        Success,
        QueueStopped,
        InvalidHandle
    };

    struct TimerEntry {
        UUID id;
        SafeCoroutineHandle handle;
        Clock::time_point deadline;
        std::atomic<bool> cancelled{false};

        bool operator>(const TimerEntry& other) const {
            return deadline > other.deadline;
        }
    };

    class TimerQueue
    {
        using EntryPtr = std::shared_ptr<TimerEntry>;

        struct TimerPtrGreater {
            bool operator()(const EntryPtr& a, const EntryPtr& b) const {
                return *a > *b;
            }
        };

        mutable std::mutex m_mtx;
        std::condition_variable_any m_cv;

        // Priority queue for timeline tracking
        std::priority_queue<EntryPtr, std::vector<EntryPtr>, TimerPtrGreater> m_timers;

        // Map for O(1) direct handle access and cancellation
        std::unordered_map<UUID, EntryPtr> m_active_map;

        std::atomic<bool> m_stopping{false};
        std::jthread m_worker;

        template <typename Callable>
        static DetachedTask LambdaCoroAdapter(std::decay_t<Callable> cb) {
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
                        m_cv.wait(lk, st, [this, &st]
                        {
                            return !m_timers.empty() || st.stop_requested();
                        });
                    }
                    else
                    {
                        auto deadline = m_timers.top()->deadline;
                        m_cv.wait_until(lk, st, deadline, [this, deadline, &st]
                        {
                            return st.stop_requested() || (!m_timers.empty() && m_timers.top()->deadline < deadline);
                        });
                    }

                    if (st.stop_requested()) break;

                    auto now = Clock::now();
                    while (!m_timers.empty() && m_timers.top()->deadline <= now)
                    {
                        EntryPtr entry = m_timers.top();
                        m_timers.pop();

                        // Remove from map if it hasn't been cancelled yet
                        m_active_map.erase(entry->id);

                        const bool is_cancelled = entry->cancelled.load(std::memory_order_relaxed);
                        lk.unlock();

                        // If not cancelled, fire the timer
                        if (!is_cancelled && entry->handle.IsValid())
                        {
                            (void)entry->handle.TryResume();
                        }

                        lk.lock();
                    }
                }
            });
        }

        bool CancelTimer(UUID uuid)
        {
            std::unique_lock lk(m_mtx);

            if (m_stopping.load(std::memory_order_relaxed)) {
                return false;
            }

            auto it = m_active_map.find(uuid);
            if (it == m_active_map.end()) {
                return false;
            }

            EntryPtr entry = it->second;
            m_active_map.erase(it);

            // Mark as cancelled so worker ignores it when deadline hits
            entry->cancelled.store(true, std::memory_order_relaxed);

            lk.unlock();

            // Trigger cancellation directly on the handle
            if (entry->handle.IsValid()) {
                entry->handle.RequestStop();
                (void)entry->handle.TryResume();
            }

            return true;
        }

        std::pair<TimerStatus, UUID> AddTimer(SafeCoroutineHandle handle, const milliseconds timeout)
        {
            if (!handle.IsValid()) {
                return {TimerStatus::InvalidHandle, {}};
            }

            std::unique_lock lk(m_mtx);
            if (m_stopping.load(std::memory_order_relaxed)) {
                return {TimerStatus::QueueStopped, {}};
            }

            auto uuid = UUID::GenerateV4();
            auto entry = std::make_shared<TimerEntry>(uuid, std::move(handle), Clock::now() + timeout);

            m_active_map[uuid] = entry;
            m_timers.push(entry);

            m_cv.notify_one();
            return {TimerStatus::Success, uuid};
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        std::pair<TimerStatus, UUID> AddTimer(std::coroutine_handle<PromiseT> handle, const milliseconds timeout)
        {
            if (!handle) return {TimerStatus::InvalidHandle, {}};
            return AddTimer(handle.promise().GetSafeHandle(), timeout);
        }

        template <typename Callable>
            requires std::invocable<std::decay_t<Callable>>
        std::pair<TimerStatus, UUID> AddTimer(Callable&& cb, const milliseconds delay) {
            auto task = LambdaCoroAdapter<Callable>(std::forward<Callable>(cb));
            return AddTimer(task.GetSafeHandle(), delay);
        }

        std::size_t GetActiveTimerCount() const noexcept
        {
            std::unique_lock lk(m_mtx);
            return m_active_map.size();
        }

        ~TimerQueue()
        {
            m_stopping.store(true, std::memory_order_relaxed);
            m_worker.request_stop();
            m_cv.notify_all();

            if (m_worker.joinable()) {
                m_worker.join();
            }

            std::unique_lock lk(m_mtx);
            for (auto& [uuid, entry] : m_active_map) {
                if (entry->handle.IsValid()) {
                    entry->handle.RequestStop();
                }
            }
            m_active_map.clear();

            while (!m_timers.empty()) {
                m_timers.pop();
            }
        }
    };
}