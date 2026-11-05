module;

#include <thread>
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

using async_core::common::SafeCoroutineHandle;

export namespace async_core::coro_loop
{
    class CoroScheduler : public ICoroScheduler
    {
        std::vector<std::unique_ptr<CoroThread>> m_workers;
        std::atomic<std::size_t> m_rrIndex{0};
        std::atomic<bool> m_stopped{false};

        void worker_loop(const std::stop_token& token, CoroThread& worker) const {
            std::queue<SafeCoroutineHandle> local_tasks;

            while (true) {
                {
                    std::unique_lock lock(worker.m_mtx);

                    worker.m_cv.wait(lock, token, [&worker] {
                        return !worker.m_tasks.empty();
                    });

                    if (worker.m_tasks.empty() && token.stop_requested()) {
                        break;
                    }

                    local_tasks.swap(worker.m_tasks);
                }

                while (!local_tasks.empty()) {
                    auto proc = std::move(local_tasks.front());
                    local_tasks.pop();
                    if (proc.is_valid()) {
                        (void)proc.try_resume();
                    }
                }
            }
        }

    public:

        explicit CoroScheduler(std::size_t numThreads = 1)
        {
            const std::size_t actualThreads = (numThreads == 0) ? 1 : numThreads;
            m_workers.reserve(actualThreads);
            for (std::size_t i = 0; i < actualThreads; ++i)
            {
                m_workers.push_back(std::make_unique<CoroThread>());
            }

            for (auto& worker : m_workers)
            {
                worker->start(&CoroScheduler::worker_loop, this);
            }
        }

        ScheduleStatus schedule_bulk(std::span<const SafeCoroutineHandle> handles) noexcept override
        {
            if (handles.empty()) return ScheduleStatus::Success;
            if (m_workers.empty() || m_stopped.load(std::memory_order_acquire)) {
                return ScheduleStatus::SchedulerStopped;
            }

            for (const auto& h : handles) {
                if (!h.is_valid()) return ScheduleStatus::InvalidHandle;
            }

            const std::size_t numWorkers = m_workers.size();
            const std::size_t startIdx = m_rrIndex.fetch_add(handles.size(), std::memory_order_relaxed);

            for (std::size_t i = 0; i < handles.size(); ++i) {
                std::size_t target = (startIdx + i) % numWorkers;
                auto status = m_workers[target]->push(handles[i]);
                if (status != ScheduleStatus::Success) {
                    return status;
                }
            }

            return ScheduleStatus::Success;
        }

        ScheduleStatus scheduleOnPosition(SafeCoroutineHandle safeHandle, std::size_t pos) noexcept override
        {
            if (!safeHandle.is_valid())
                return ScheduleStatus::InvalidHandle;

            if (m_workers.empty() || m_stopped.load(std::memory_order_acquire))
                return ScheduleStatus::SchedulerStopped;

            std::size_t targetIdx = pos % m_workers.size();
            return m_workers[targetIdx]->push(std::move(safeHandle));
        }

        ScheduleStatus schedule(SafeCoroutineHandle safeHandle) noexcept override {
            return scheduleOnPosition(std::move(safeHandle), m_rrIndex.fetch_add(1, std::memory_order_relaxed));
        }

        void stop_and_wait_all() override
        {
            request_stop();
            for (const auto& worker : m_workers) {
                worker->join();
            }
        }

        [[nodiscard]] std::size_t worker_count() const noexcept override {
            return m_workers.size();
        }

        void request_stop() noexcept override
        {
            if (bool expected = false; m_stopped.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {

                for (const auto& worker : m_workers) {
                    worker->request_stop();
                }
            }
        }

        ~CoroScheduler() override
        {
            CoroScheduler::stop_and_wait_all();
        }
    };
}