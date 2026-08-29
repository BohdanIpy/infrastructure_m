module;

#include <coroutine>
#include <cstddef>
#include <span>
#include <system_error>
#include <tuple>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncReadSocketFrom;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncReadSocketFrom
    {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        std::span<std::byte> m_buffer{};
        const int m_flags{0};
        IoCtx m_ctx{};

    public:
        explicit AsyncReadSocketFrom(
            IoQueue* queue,
            const SocketT fd,
            std::span<std::byte> buffer,
            const int flags = 0
        ) noexcept
            : m_queue(queue), m_fd(fd), m_buffer(buffer), m_flags(flags) {}

        AsyncReadSocketFrom(const AsyncReadSocketFrom&) = delete;
        AsyncReadSocketFrom& operator=(const AsyncReadSocketFrom&) = delete;
        AsyncReadSocketFrom(AsyncReadSocketFrom&&) = delete;
        AsyncReadSocketFrom& operator=(AsyncReadSocketFrom&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept {
            return m_buffer.empty();
        }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_udp_recv(m_buffer);

            m_queue->submit_read_socket_from(
                m_fd,
                &m_ctx,
                m_flags
            );
        }

        [[nodiscard]]
        std::tuple<std::size_t, sockaddr_storage, socklen_t> await_resume() const {
            if (m_buffer.empty()) {
                return {0, {}, 0};
            }

            if (m_ctx.result < 0) {
                throw std::system_error(
                    -m_ctx.result,
                    std::generic_category(),
                    "Async UDP socket read failed"
                );
            }

            const auto& dg = m_ctx.payload.datagram;
            return std::make_tuple(
                static_cast<std::size_t>(m_ctx.result),
                dg.remote_addr,
                dg.msg.msg_namelen
            );
        }
    };
}