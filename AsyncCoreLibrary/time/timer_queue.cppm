module;

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <utility>

export module AsyncCoreLibrary:TimerQueue;

import :ICoroScheduler;

namespace async_core::time {
using coro_loop::ICoroScheduler;
}

export namespace async_core::time {
using Clock = std::chrono::steady_clock;
using TimerId = std::uint64_t; 

enum class TimerOutcome : std::uint8_t {
    Fired,
    Shutdown 
};

class TimerQueue {
  public:
    using Callback = std::function<void(TimerOutcome)>;

  private:
    using Key = std::pair<Clock::time_point, TimerId>;

    ICoroScheduler& m_scheduler;
    mutable std::mutex m_mtx;
    std::condition_variable_any m_cv;
    std::map<Key, Callback> m_timers; 
    std::unordered_map<TimerId, Clock::time_point> m_index; 
    TimerId m_next_id{1};
    bool m_stopping{false};
    std::jthread m_worker;

    static void invoke(Callback& callback, TimerOutcome outcome) noexcept {
        try {
            callback(outcome);
        } catch (...) {
            // a callback must not throw; a throwing one must not kill the timer thread
        }
    }

    void run(const std::stop_token& stop) {
        std::unique_lock lock(m_mtx);
        while (!stop.stop_requested()) {
            if (m_timers.empty()) {
                m_cv.wait(lock, stop, [this] { return !m_timers.empty(); });
                continue;
            }

            const Clock::time_point next = m_timers.begin()->first.first;
            if (Clock::now() < next) {
                m_cv.wait_until(lock, stop, next, [this, next] {
                    return m_timers.empty() || m_timers.begin()->first.first < next;
                });
                continue;
            }

            auto node = m_timers.extract(
                m_timers.begin()); // removed BEFORE the callback runs: Cancel() now says false
            m_index.erase(node.key().second);
            lock.unlock();
            invoke(node.mapped(), TimerOutcome::Fired);
            lock.lock();
        }
    }

  public:
    explicit TimerQueue(ICoroScheduler& scheduler)
        : m_scheduler(scheduler) {
        m_worker = std::jthread([this](const std::stop_token stop) { run(stop); });
    }

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;
    TimerQueue& operator=(TimerQueue&&) = delete;

    ~TimerQueue() {
        {
            std::lock_guard lock(m_mtx);
            m_stopping = true; 
        }
        m_worker.request_stop(); 
        m_worker.join();

        std::map<Key, Callback> pending;
        {
            std::lock_guard lock(m_mtx);
            pending.swap(m_timers);
            m_index.clear();
        }
        for (auto& [key, callback] : pending) {
            invoke(callback, TimerOutcome::Shutdown);
        }
    }

    [[nodiscard]] ICoroScheduler& scheduler() const noexcept { return m_scheduler; }

    [[nodiscard]] std::optional<TimerId> AddTimer(const Clock::time_point deadline,
                                                  Callback callback) {
        std::lock_guard lock(m_mtx);
        if (m_stopping) {
            return std::nullopt;
        }
        const TimerId id = m_next_id++;
        m_timers.emplace(Key{deadline, id}, std::move(callback));
        try {
            m_index.emplace(id, deadline);
        } catch (...) {
            m_timers.erase(Key{deadline, id});
            throw;
        }
        if (m_timers.begin()->first.second == id) {
            m_cv.notify_one(); 
        }
        return id;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] std::optional<TimerId> AddTimer(const std::chrono::duration<Rep, Period> delay,
                                                  Callback callback) {
        return AddTimer(Clock::now() + std::chrono::ceil<Clock::duration>(delay),
                        std::move(callback));
    }

    bool CancelTimer(const TimerId id) noexcept {
        Callback dropped; 
        std::lock_guard lock(m_mtx);
        const auto index = m_index.find(id);
        if (index == m_index.end()) {
            return false;
        }
        const auto timer = m_timers.find(Key{index->second, id});
        if (timer != m_timers.end()) {
            dropped = std::move(timer->second);
            m_timers.erase(timer);
        }
        m_index.erase(index);
        return true;
    }

    [[nodiscard]] std::size_t GetActiveTimerCount() const noexcept {
        std::lock_guard lock(m_mtx);
        return m_timers.size();
    }
};
} // namespace async_core::time