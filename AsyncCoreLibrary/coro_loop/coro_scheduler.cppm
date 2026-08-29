module;

#include <thread>
#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <bits/unique_lock.h>


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
        const std::stop_source m_source{};
        std::vector<std::unique_ptr<CoroThread>> m_workers;
        std::atomic<std::size_t> m_rrIndex{0};

        void worker_loop(CoroThread& worker, const std::stop_token& token) const {
            while (!token.stop_requested())
            {
                SafeCoroutineHandle proc;
                {
                    std::unique_lock<std::mutex> lock(worker.m_mtx);
                    worker.m_cv.wait(lock, token, [&worker] {
                        return !worker.m_tasks.empty();
                    });

                    if (token.stop_requested() && worker.m_tasks.empty()) {
                        return;
                    }

                    if (!worker.m_tasks.empty()) {
                        proc = std::move(worker.m_tasks.front());
                        worker.m_tasks.pop();
                    }
                }

                if (proc.is_valid()) {
                    proc.try_resume();
                }
            }
        }

    public:
        explicit CoroScheduler(std::size_t numThreads = 1)
        {
            m_workers.reserve(numThreads);
            for (std::size_t i = 0; i < numThreads; ++i)
            {
                m_workers.push_back(
                    std::make_unique<CoroThread>(
                        &CoroScheduler::worker_loop,
                        this,
                        m_source.get_token()
                    )
                );
            }
        }

        void scheduleOnPosition(SafeCoroutineHandle safeHandle, std::size_t pos) noexcept override
        {
            if (!safeHandle.is_valid() || m_workers.empty()) return;

            std::size_t targetIdx = pos % m_workers.size();
            m_workers[targetIdx]->push(std::move(safeHandle));
        }

        void schedule(SafeCoroutineHandle safeHandle) noexcept override {
            scheduleOnPosition(std::move(safeHandle), m_rrIndex.fetch_add(1, std::memory_order_relaxed));
        }

        void stop_and_wait_all() override
        {
            if (!m_source.stop_requested()) {
                (void)m_source.request_stop();
                std::cout << "[ServiceRunner] Initiating shutdown for all services...\n";
                for (const auto& worker : m_workers) {
                    worker->m_cv.notify_all();
                }
                m_workers.clear();
                std::cout << "[ServiceRunner] All services stopped successfully.\n";
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