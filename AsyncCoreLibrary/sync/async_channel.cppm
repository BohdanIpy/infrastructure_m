module;

#include <queue>
#include <mutex>
#include <optional>
#include <atomic>
#include <iostream>
#include <coroutine>
#include <type_traits>
#include <utility>
#include <memory>

export module AsyncCoreLibrary:AsyncChannel;

import :ICoroScheduler;
import :SafeCoroutineHandle;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace async_core::sync
{
    template <typename PromiseT>
    concept HasSafeHandle = requires(PromiseT& p) {
        { p.get_safe_handle() } -> std::same_as<SafeCoroutineHandle>;
    };

    template <typename T>
    class AsyncChannel {
        ICoroScheduler* m_scheduler{nullptr};
        const size_t m_capacity{0};
        bool m_closed{false};

        std::queue<T> m_buffer;
        std::mutex m_mtx;

        struct ReaderNode {
            SafeCoroutineHandle handle;
            std::shared_ptr<std::optional<T>> value_slot;
        };

        struct WriterNode {
            SafeCoroutineHandle handle;
            T value;
            std::shared_ptr<bool> success_slot;
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

        // --- Awaiters Defined as Class-Level Nested Structs ---

        struct SendAwaiter {
            AsyncChannel* channel;
            T value;
            std::shared_ptr<bool> sent_successfully{std::make_shared<bool>(false)};

            bool await_ready() noexcept {
                std::lock_guard lock(channel->m_mtx);
                return try_send_under_lock();
            }

            bool try_send_under_lock() noexcept {
                if (channel->m_closed) return true;

                if (!channel->m_readers.empty()) {
                    auto reader = std::move(channel->m_readers.front());
                    channel->m_readers.pop();

                    if (reader.value_slot) {
                        *reader.value_slot = std::move(value);
                    }
                    *sent_successfully = true;
                    channel->debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                    channel->debug_receives_succeeded.fetch_add(1, std::memory_order_relaxed);

                    if (reader.handle.is_valid()) {
                        if (channel->m_scheduler) channel->m_scheduler->schedule(std::move(reader.handle));
                        else reader.handle.try_resume();
                    }
                    return true;
                }

                if (channel->m_capacity > 0 && channel->m_buffer.size() < channel->m_capacity) {
                    channel->m_buffer.push(std::move(value));
                    *sent_successfully = true;
                    channel->debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }

                return false;
            }

            template <typename PromiseT>
                requires HasSafeHandle<PromiseT>
            bool await_suspend(std::coroutine_handle<PromiseT> h) noexcept {
                std::lock_guard lock(channel->m_mtx);

                if (try_send_under_lock()) {
                    return false;
                }

                channel->debug_writers_pushed.fetch_add(1, std::memory_order_relaxed);
                channel->m_writers.push(WriterNode{
                    h.promise().get_safe_handle(),
                    std::move(value),
                    sent_successfully
                });
                return true;
            }

            bool await_resume() const noexcept {
                if (!*sent_successfully) {
                    channel->debug_sends_failed.fetch_add(1, std::memory_order_relaxed);
                }
                return *sent_successfully;
            }
        };

        struct ReceiveAwaiter {
            AsyncChannel* channel;
            std::shared_ptr<std::optional<T>> result_slot{std::make_shared<std::optional<T>>(std::nullopt)};

            bool await_ready() noexcept {
                std::lock_guard lock(channel->m_mtx);
                return try_fetch_under_lock();
            }

            bool try_fetch_under_lock() noexcept {
                if (!channel->m_buffer.empty()) {
                    *result_slot = std::move(channel->m_buffer.front());
                    channel->m_buffer.pop();
                    channel->debug_receives_succeeded.fetch_add(1, std::memory_order_relaxed);

                    if (!channel->m_writers.empty()) {
                        auto writer = std::move(channel->m_writers.front());
                        channel->m_writers.pop();

                        channel->m_buffer.push(std::move(writer.value));
                        if (writer.success_slot) *writer.success_slot = true;
                        channel->debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                        channel->debug_writers_drained_by_receive.fetch_add(1, std::memory_order_relaxed);

                        if (writer.handle.is_valid()) {
                            if (channel->m_scheduler) channel->m_scheduler->schedule(std::move(writer.handle));
                            else writer.handle.try_resume();
                        }
                    }
                    return true;
                }

                if (!channel->m_writers.empty()) {
                    auto writer = std::move(channel->m_writers.front());
                    channel->m_writers.pop();

                    *result_slot = std::move(writer.value);
                    if (writer.success_slot) *writer.success_slot = true;
                    channel->debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);
                    channel->debug_receives_succeeded.fetch_add(1, std::memory_order_relaxed);
                    channel->debug_writers_drained_by_receive.fetch_add(1, std::memory_order_relaxed);

                    if (writer.handle.is_valid()) {
                        if (channel->m_scheduler) channel->m_scheduler->schedule(std::move(writer.handle));
                        else writer.handle.try_resume();
                    }
                    return true;
                }

                if (channel->m_closed) {
                    *result_slot = std::nullopt;
                    return true;
                }

                return false;
            }

            template <typename PromiseT>
                requires HasSafeHandle<PromiseT>
            bool await_suspend(std::coroutine_handle<PromiseT> h) noexcept {
                std::lock_guard lock(channel->m_mtx);

                if (try_fetch_under_lock()) {
                    return false;
                }

                channel->m_readers.push(ReaderNode{
                    h.promise().get_safe_handle(),
                    result_slot
                });
                return true;
            }

            std::optional<T> await_resume() noexcept {
                return std::move(*result_slot);
            }
        };

        // --- Channel Interface ---

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
                    auto writer = std::move(m_writers.front());
                    m_writers.pop();
                    m_buffer.push(std::move(writer.value));

                    if (writer.success_slot) *writer.success_slot = true;
                    debug_sends_succeeded.fetch_add(1, std::memory_order_relaxed);

                    if (writer.handle.is_valid()) {
                        if (m_scheduler) m_scheduler->schedule(std::move(writer.handle));
                        else writer.handle.try_resume();
                    }
                }

                std::swap(readers_to_wake, m_readers);
                std::swap(writers_to_wake, m_writers);
            }

            while (!readers_to_wake.empty()) {
                auto node = std::move(readers_to_wake.front());
                readers_to_wake.pop();
                if (node.value_slot) *node.value_slot = std::nullopt;
                if (node.handle.is_valid()) {
                    if (m_scheduler) m_scheduler->schedule(std::move(node.handle));
                    else node.handle.try_resume();
                }
            }

            while (!writers_to_wake.empty()) {
                auto node = std::move(writers_to_wake.front());
                writers_to_wake.pop();
                if (node.success_slot) *node.success_slot = false;
                debug_writers_failed_by_close.fetch_add(1, std::memory_order_relaxed);
                debug_sends_failed.fetch_add(1, std::memory_order_relaxed);
                if (node.handle.is_valid()) {
                    if (m_scheduler) m_scheduler->schedule(std::move(node.handle));
                    else node.handle.try_resume();
                }
            }
        }

        [[nodiscard]] SendAwaiter send(T value) noexcept {
            debug_sends_attempted.fetch_add(1, std::memory_order_relaxed);
            return SendAwaiter{.channel = this, .value = std::move(value)};
        }

        [[nodiscard]] ReceiveAwaiter receive() noexcept {
            return ReceiveAwaiter{.channel = this};
        }
    };
}