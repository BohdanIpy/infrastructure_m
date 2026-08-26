module;

#include <coroutine>
#include <condition_variable>
#include <thread>
#include <queue>
#include <iostream>
#include <vector>
#include <mutex>

export module AsyncCoreLibrary:CoroScheduler;

import :ICoroScheduler;

export namespace async_core::coro_loop
{
    class CoroScheduler : public ICoroScheduler
    {
        std::queue<std::coroutine_handle<>> m_coroQ;
        std::stop_source m_source{};
        std::vector<std::jthread> m_workers;
        std::mutex m_queueMtx;
        std::condition_variable_any m_cv;

        void worker_loop(std::stop_token token)
        {
            while (!token.stop_requested())
            {
                std::coroutine_handle<> proc;
                {
                    std::unique_lock lock(m_queueMtx);

                    m_cv.wait(lock, token, [this] {
                        return !m_coroQ.empty();
                    });

                    if (token.stop_requested() && m_coroQ.empty()) {
                        return;
                    }

                    if (!m_coroQ.empty()) {
                        proc = m_coroQ.front();
                        m_coroQ.pop();
                    }
                }
                if (proc) {
                    proc.resume();
                }
            }
        }

    public:
        explicit CoroScheduler(std::size_t numThreads)
        {
            m_workers.reserve(numThreads);
            for (std::size_t i = 0; i < numThreads; ++i)
            {
                m_workers.emplace_back([this](std::stop_token) {
                    worker_loop(m_source.get_token());
                });
            }
        }

        void schedule(std::coroutine_handle<> h) noexcept override {
            if (!h) return;
            {
                std::lock_guard lock(m_queueMtx);
                m_coroQ.push(h);
            }
            m_cv.notify_one();
        }

        void stop_and_wait_all() override
        {
            // Atomically request stop once
            if (!m_source.stop_requested()) {
                (void)m_source.request_stop();

                std::cout << "[ServiceRunner] Initiating shutdown for all services...\n";
                m_cv.notify_all();
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
            stop_and_wait_all();
        }
    };
}