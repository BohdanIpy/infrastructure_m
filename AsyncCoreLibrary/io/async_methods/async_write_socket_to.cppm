module;

#include <coroutine>
#include <cstddef>
#include <span>
#include <system_error>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncWriteSocketTo;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncWriteSocketTo
    {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        const std::span<std::byte> m_buffer{};
        const sockaddr* m_dest_addr{nullptr};
        const socklen_t m_dest_len{0};
        int m_flags{0};
        IoCtx m_ctx{};

    public:
        explicit AsyncWriteSocketTo(
            IoQueue* queue,
            const SocketT fd,
            const std::span<std::byte> buffer,
            const sockaddr* dest_addr,
            const socklen_t dest_len,
            const int flags = 0
        ) noexcept
            : m_queue(queue),
              m_fd(fd),
              m_buffer(buffer),
              m_dest_addr(dest_addr),
              m_dest_len(dest_len),
              m_flags(flags) {}

        AsyncWriteSocketTo(const AsyncWriteSocketTo&) = delete;
        AsyncWriteSocketTo& operator=(const AsyncWriteSocketTo&) = delete;
        AsyncWriteSocketTo(AsyncWriteSocketTo&&) = delete;
        AsyncWriteSocketTo& operator=(AsyncWriteSocketTo&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept {
            return m_buffer.empty();
        }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_udp_send(
                m_buffer,
                m_dest_addr,
                m_dest_len
            );

            m_queue->submit_udp_socket_write(
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
                    "Async UDP socket write failed"
                );
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };
}