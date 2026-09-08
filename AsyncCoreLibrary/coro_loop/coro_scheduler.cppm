module;

#include <thread>
#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

export module AsyncCoreLibrary:CoroScheduler;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :CoroThread;

using async_core::coro_thread::CoroThread;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::coro_loop
{
    class CoroScheduler : public ICoroScheduler
    {
        std::vector<std::unique_ptr<CoroThread>> m_workers;
        std::atomic<std::size_t> m_rrIndex{0};
        std::atomic<bool> m_stopped{false};

        void worker_loop(const std::stop_token& token, CoroThread& worker) const {
            while (true) {
                std::shared_ptr<SafeCoroutineHandle> proc;
                {
                    std::unique_lock lock(worker.m_mtx);

                    worker.m_cv.wait(lock, token, [&worker] {
                        return !worker.m_tasks.empty();
                    });

                    if (worker.m_tasks.empty()) {
                        if (token.stop_requested()) {
                            break;
                        }
                        continue;
                    }

                    proc = std::move(worker.m_tasks.front());
                    worker.m_tasks.pop();
                }
                if (proc->is_valid()) {
                    proc->try_resume();
                }
            }
        }

    public:
        explicit CoroScheduler(std::size_t numThreads = 1)
        {
            m_workers.reserve(numThreads);
            for (std::size_t i = 0; i < numThreads; ++i)
            {
                m_workers.push_back(std::make_unique<CoroThread>());
            }

            for (auto& worker : m_workers)
            {
                worker->start(&CoroScheduler::worker_loop, this);
            }
        }

        void scheduleOnPosition(std::shared_ptr<exec::SafeCoroutineHandle> safeHandle, std::size_t pos) noexcept override
        {
            if (!safeHandle->is_valid() || m_workers.empty() || m_stopped.load(std::memory_order_relaxed)) {
                return;
            }

            std::size_t targetIdx = pos % m_workers.size();
            m_workers[targetIdx]->push(std::move(safeHandle));
        }

        void schedule(std::shared_ptr<exec::SafeCoroutineHandle> safeHandle) noexcept override {
            scheduleOnPosition(std::move(safeHandle), m_rrIndex.fetch_add(1, std::memory_order_relaxed));
        }

        void stop_and_wait_all() override
        {
            bool expected = false;
            if (m_stopped.compare_exchange_strong(expected, true)) {
                std::cout << "[CoroScheduler] Initiating shutdown for all workers...\n";

                for (const auto& worker : m_workers) {
                    worker->request_stop();
                }

                m_workers.clear();
                std::cout << "[CoroScheduler] All workers stopped successfully.\n";
            }
        }

        void request_stop() override
        {
            stop_and_wait_all();
        }

        ~CoroScheduler() override
        {
            CoroScheduler::stop_and_wait_all();
        }
    };
}