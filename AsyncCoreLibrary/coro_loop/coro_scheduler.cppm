module;

#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <span>
#include <queue>

export module AsyncCoreLibrary:CoroScheduler;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :CoroThread;
import :ScheduleTypes;
import :BulkScheduleResult;
import CoreUtility;

namespace async_core::coro_loop {
using common::SafeCoroutineHandle;
using core_utility::stderr_logger::GetLogger;
} // namespace async_core::coro_loop

export namespace async_core::coro_loop {
/**
 * @brief Main scheduler responsible for scheduling and running coroutines
 */
class CoroScheduler : public ICoroScheduler {
    std::vector<std::unique_ptr<CoroThread>> m_workers;
    std::atomic<std::uint64_t> m_rrIndex{0};
    std::atomic<bool> m_stopped{false};

    /**
     * @brief Main logic of scheduler, runs on CoroThread
     *
     * @param token cancellation utilities
     * @param worker instance of CoroThread holding a queue of Tasks and more
     */
    void worker_loop(const std::stop_token& token, CoroThread& worker) const {
        std::queue<SafeCoroutineHandle> local_tasks;

        while (true) {
            {
                std::unique_lock lock(worker.m_mtx);

                worker.m_cv.wait(lock, token, [&worker] { return !worker.m_tasks.empty(); });

                if (worker.m_tasks.empty() && token.stop_requested()) {
                    break;
                }

                local_tasks.swap(worker.m_tasks);
            }

            while (!local_tasks.empty()) {
                auto proc = std::move(local_tasks.front());
                local_tasks.pop();
                if (proc.IsValid()) {
                    try {
                        (void)proc.TryResume();
                    } catch (...) {
                        GetLogger().LogCritical(
                            "CoroScheduler", "Exception escaped a coroutine; worker keeps running");
                    }
                }
            }
        }
    }

  public:
    explicit CoroScheduler(std::uint64_t numThreads = 1) {
        const std::uint64_t actualThreads = (numThreads == 0) ? 1 : numThreads;
        m_workers.reserve(actualThreads);
        for (std::uint64_t i = 0; i < actualThreads; ++i) {
            m_workers.push_back(std::make_unique<CoroThread>());
        }

        for (auto& worker : m_workers) {
            worker->start(
                [this, w = worker.get()](const std::stop_token& token) { worker_loop(token, *w); });
        }
    }

    /**
     * @brief Schedule multiple coroutines as a pack
     *
     * @param handles span hloding handles to schedule
     * @return BulkScheduleResult CAN PARTIALLY SCHEDULE. Returns a result of scheduling.
     */
    [[nodiscard]] BulkScheduleResult
    ScheduleBulk(std::span<const SafeCoroutineHandle> handles) noexcept override {
        if (handles.empty())
            return {.status = ScheduleStatus::Success, .scheduled = 0};
        if (m_workers.empty() || m_stopped.load(std::memory_order_acquire)) {
            return {.status = ScheduleStatus::SchedulerStopped, .scheduled = 0};
        }

        for (const auto& h : handles) {
            if (!h.IsValid())
                return {.status = ScheduleStatus::InvalidHandle, .scheduled = 0};
        }

        const std::uint64_t numWorkers = m_workers.size();
        const std::uint64_t startIdx =
            m_rrIndex.fetch_add(handles.size(), std::memory_order_relaxed);

        std::uint64_t scheduled = 0;
        for (const auto& h : handles) {
            const ScheduleStatus status = m_workers[(startIdx + scheduled) % numWorkers]->push(h);
            if (status != ScheduleStatus::Success) {
                return {.status = status, .scheduled = scheduled};
            }
            ++scheduled;
        }

        return {.status = ScheduleStatus::Success, .scheduled = scheduled};
    }

    /**
     * @brief Schedule a coroutine on precise thread. Required for some functionality
     * 
     * @param safeHandle Coroutine handle
     * @param pos position(thread) on which schedule
     * @return ScheduleStatus status of scheduling 
     */
    ScheduleStatus ScheduleOnPosition(SafeCoroutineHandle safeHandle,
                                      std::uint64_t pos) noexcept override {
        if (!safeHandle.IsValid())
            return ScheduleStatus::InvalidHandle;

        if (m_workers.empty() || m_stopped.load(std::memory_order_acquire))
            return ScheduleStatus::SchedulerStopped;

        std::uint64_t targetIdx = pos % m_workers.size();
        return m_workers[targetIdx]->push(std::move(safeHandle));
    }

    /**
     * @brief Schedule the coroutine
     * 
     * @param safeHandle Coroutine handle
     * @return ScheduleStatus status of scheduling
     */
    ScheduleStatus Schedule(SafeCoroutineHandle safeHandle) noexcept override {
        return ScheduleOnPosition(std::move(safeHandle),
                                  m_rrIndex.fetch_add(1, std::memory_order_relaxed));
    }

    /**
     * @brief Request stop and wait for all worker threads to stop.
     */
    void StopAndWaitAll() override {
        RequestStop();
        for (const auto& worker : m_workers) {
            worker->join();
        }
    }
    /**
     * @brief Returns number of workers
     * @return std::uint64_t number of workers  
     */
    [[nodiscard]] std::uint64_t WorkerCount() const noexcept override { return m_workers.size(); }

    /**
     * @brief Mark the scheduler as stopped, does not schedule the next pushed coroutines, and drain presented coroutines
     * 
     */
    void RequestStop() noexcept override {
        if (bool expected = false;
            m_stopped.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {

            for (const auto& worker : m_workers) {
                worker->request_stop();
            }
        }
    }

    ~CoroScheduler() override { CoroScheduler::StopAndWaitAll(); }
};
} // namespace async_core::coro_loop
