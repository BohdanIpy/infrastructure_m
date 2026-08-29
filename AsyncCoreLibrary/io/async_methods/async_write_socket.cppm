module;

#include <coroutine>
#include <system_error>
#include <span>
#include <cstddef>

export module AsyncCoreLibrary:AsyncWriteSocket;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncWriteSocket
    {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        const std::span<std::byte> m_buffer;
        const int m_flags{0};
        IoCtx m_ctx{};

    public:
        explicit AsyncWriteSocket(
            IoQueue* queue,
            const SocketT fd,
            const std::span<std::byte> buffer,
            const int flags = 0
        ) noexcept
            : m_queue(queue), m_fd(fd), m_buffer(buffer), m_flags(flags)
        {}

        AsyncWriteSocket(const AsyncWriteSocket&) = delete;
        AsyncWriteSocket& operator=(const AsyncWriteSocket&) = delete;
        AsyncWriteSocket(AsyncWriteSocket&&) = delete;
        AsyncWriteSocket& operator=(AsyncWriteSocket&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept {
            return m_buffer.empty();
        }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_stream_write(m_buffer);

            m_queue->submit_tcp_socket_write(
                m_fd,
                &m_ctx,
                m_flags
            );
        }

        [[nodiscard]]
        std::size_t await_resume() const {
            if (m_buffer.empty()) {
                return 0;
            }

            if (m_ctx.result < 0) {
                throw std::system_error(
                    -m_ctx.result,
                    std::generic_category(),
                    "Async socket write failed"
                );
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };
}