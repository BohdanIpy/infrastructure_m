module;

#include <queue>
#include <mutex>
#include <optional>
#include <atomic>
#include <iostream>
#include <coroutine>

export module AsyncCoreLibrary:AsyncChannel;

import :ICoroScheduler;

using async_core::coro_loop::ICoroScheduler;

export namespace async_core::sync
{
    template <typename T>
    class AsyncChannel {
        ICoroScheduler* m_scheduler{nullptr};
        const size_t m_capacity{0};
        bool m_closed{false};

        std::queue<T> m_buffer;
        std::mutex m_mtx;

        struct ReaderNode {
            std::coroutine_handle<> handle;
            std::optional<T>* value_slot;
        };

        struct WriterNode {
            std::coroutine_handle<> handle;
            T value;
            bool* success_slot{nullptr};
        };

        std::queue<ReaderNode> m_readers;
        std::queue<WriterNode> m_writers;

    public:
        std::atomic<int> debug_sends_attempted{0};
        std::atomic<int> debug_sends_succeeded{0};
        std::atomic<int> debug_sends_failed{0};
        std::atomic<int> debug_receives_succeeded{0};
        std::atomic<int> debug_writers_pushed{0};
        std::atomic<int> debug_writers_drained_by_receive{0};
        std::atomic<int> debug_writers_failed_by_close{0};

        explicit AsyncChannel(ICoroScheduler* sched = nullptr, size_t capacity = 0)
            : m_scheduler(sched), m_capacity(capacity) {}

        void set_scheduler(ICoroScheduler* sched) noexcept {
            m_scheduler = sched;
        }

        void dump_diagnostics() {
            std::lock_guard lock(m_mtx);
            std::cout << "\n==========================================" << std::endl;
            std::cout << "         ASYNC CHANNEL DIAGNOSTICS        " << std::endl;
            std::cout << "==========================================" << std::endl;
            std::cout << "Sends Attempted:            " << debug_sends_attempted.load() << std::endl;
            std::cout << "Sends Succeeded:            " << debug_sends_succeeded.load() << std::endl;
            std::cout << "Sends Failed:               " << debug_sends_failed.load() << std::endl;
            std::cout << "Receives Succeeded:         " << debug_receives_succeeded.load() << std::endl;
            std::cout << "Remaining in Buffer:        " << m_buffer.size() << std::endl;
            std::cout << "Writers Suspended:          " << debug_writers_pushed.load() << std::endl;
            std::cout << "Writers Drained by Receive: " << debug_writers_drained_by_receive.load() << std::endl;
            std::cout << "Writers Failed by Close:    " << debug_writers_failed_by_close.load() << std::endl;
            std::cout << "Is Closed:                  " << (m_closed ? "true" : "false") << std::endl;
            std::cout << "==========================================\n" << std::endl;
        }

        void close() noexcept {
            std::queue<ReaderNode> readers_to_wake;
            std::queue<WriterNode> writers_to_wake;

            {
                std::lock_guard lock(m_mtx);
                if (m_closed) return;
                m_closed = true;

                while (!m_writers.empty() && (m_capacity == 0 || m_buffer.size() < m_capacity)) {
                    auto writer = m_writers.front();
                    m_writers.pop();
                    m_buffer.push(std::move(writer.value));
                    if (writer.success_slot) *writer.success_slot = true;
                    debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                    if (writer.handle && m_scheduler) {
                        m_scheduler->schedule(writer.handle);
                    }
                }

                std::swap(readers_to_wake, m_readers);
                std::swap(writers_to_wake, m_writers);
            }

            while (!readers_to_wake.empty()) {
                auto node = readers_to_wake.front();
                readers_to_wake.pop();
                if (node.value_slot) *node.value_slot = std::nullopt;
                if (node.handle && m_scheduler) m_scheduler->schedule(node.handle);
            }

            while (!writers_to_wake.empty()) {
                auto node = writers_to_wake.front();
                writers_to_wake.pop();
                if (node.success_slot) *node.success_slot = false;
                debug_writers_failed_by_close.fetch_add(1, std::memory_order_relaxed);
                debug_sends_failed.fetch_add(1, std::memory_order_relaxed);
                if (node.handle && m_scheduler) m_scheduler->schedule(node.handle);
            }
        }

        [[nodiscard]] auto send(T value) noexcept {
            debug_sends_attempted.fetch_add(1, std::memory_order_relaxed);

            struct SendAwaiter {
                AsyncChannel& channel;
                T value;
                bool sent_successfully{false};

                bool try_send_under_lock() noexcept {
                    if (channel.m_closed) return true;

                    if (!channel.m_readers.empty()) {
                        auto reader = channel.m_readers.front();
                        channel.m_readers.pop();

                        if (reader.value_slot) {
                            *reader.value_slot = std::move(value);
                        }
                        sent_successfully = true;
                        channel.debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                        channel.debug_receives_succeeded.fetch_add(1, std::memory_order_relaxed);

                        if (reader.handle && channel.m_scheduler) {
                            channel.m_scheduler->schedule(reader.handle);
                        }
                        return true;
                    }

                    if (channel.m_capacity == 0 || channel.m_buffer.size() < channel.m_capacity) {
                        channel.m_buffer.push(std::move(value));
                        sent_successfully = true;
                        channel.debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                        return true;
                    }

                    return false;
                }

                bool await_ready() noexcept {
                    std::lock_guard lock(channel.m_mtx);
                    return try_send_under_lock();
                }

                bool await_suspend(std::coroutine_handle<> h) noexcept {
                    std::lock_guard lock(channel.m_mtx);

                    if (try_send_under_lock()) {
                        return false;
                    }

                    channel.debug_writers_pushed.fetch_add(1, std::memory_order_relaxed);
                    channel.m_writers.push(WriterNode{h, std::move(value), &sent_successfully});
                    return true;
                }

                bool await_resume() noexcept {
                    if (!sent_successfully) {
                        channel.debug_sends_failed.fetch_add(1, std::memory_order_relaxed);
                    }
                    return sent_successfully;
                }
            };

            return SendAwaiter{*this, std::move(value)};
        }

        [[nodiscard]] auto receive() noexcept {
            struct ReceiveAwaiter {
                AsyncChannel& channel;
                std::optional<T> result_slot{std::nullopt};

                bool try_fetch_under_lock() noexcept {
                    if (!channel.m_buffer.empty()) {
                        result_slot = std::move(channel.m_buffer.front());
                        channel.m_buffer.pop();
                        channel.debug_receives_succeeded.fetch_add(1, std::memory_order_relaxed);

                        if (!channel.m_writers.empty()) {
                            auto writer = channel.m_writers.front();
                            channel.m_writers.pop();

                            channel.m_buffer.push(std::move(writer.value));
                            if (writer.success_slot) *writer.success_slot = true;
                            channel.debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                            channel.debug_writers_drained_by_receive.fetch_add(1, std::memory_order_relaxed);
                            if (writer.handle && channel.m_scheduler) {
                                channel.m_scheduler->schedule(writer.handle);
                            }
                        }
                        return true;
                    }

                    if (!channel.m_writers.empty()) {
                        auto writer = channel.m_writers.front();
                        channel.m_writers.pop();

                        result_slot = std::move(writer.value);
                        if (writer.success_slot) *writer.success_slot = true;
                        channel.debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                        channel.debug_receives_succeeded.fetch_add(1, std::memory_order_relaxed);
                        channel.debug_writers_drained_by_receive.fetch_add(1, std::memory_order_relaxed);
                        if (writer.handle && channel.m_scheduler) {
                            channel.m_scheduler->schedule(writer.handle);
                        }
                        return true;
                    }

                    if (channel.m_closed) {
                        result_slot = std::nullopt;
                        return true;
                    }

                    return false;
                }

                bool await_ready() noexcept {
                    std::lock_guard lock(channel.m_mtx);
                    return try_fetch_under_lock();
                }

                bool await_suspend(std::coroutine_handle<> h) noexcept {
                    std::lock_guard lock(channel.m_mtx);

                    if (try_fetch_under_lock()) {
                        return false;
                    }

                    channel.m_readers.push(ReaderNode{h, &result_slot});
                    return true;
                }

                std::optional<T> await_resume() noexcept {
                    return std::move(result_slot);
                }
            };

            return ReceiveAwaiter{*this};
        }
    };

}