module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncReadSocketFrom;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    struct ReceiveFromResult
    {
        std::size_t bytes{0};
        SocketAddress peer{};
    };

    /// recvmsg() for datagram sockets: one datagram and the address it came from.
    class AsyncReadSocketFrom final : public IoOperation<AsyncReadSocketFrom>
    {
        friend class IoOperation<AsyncReadSocketFrom>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        std::span<std::byte> m_buffer;
        int m_flags;
        sockaddr_storage m_addr{};
        iovec m_iov{};
        msghdr m_msg{};

        void prepare(io_uring_sqe* sqe) noexcept
        {
            m_iov.iov_base = m_buffer.data();
            m_iov.iov_len = m_buffer.size();
            m_msg.msg_name = &m_addr;
            m_msg.msg_namelen = sizeof(m_addr);
            m_msg.msg_iov = &m_iov;
            m_msg.msg_iovlen = 1;
            io_uring_prep_recvmsg(sqe, m_fd, &m_msg, static_cast<unsigned>(m_flags));
        }
        [[nodiscard]] bool nothing_to_do() const noexcept { return m_buffer.empty(); }

    public:
        AsyncReadSocketFrom(IoQueue& queue, const SocketT fd, const std::span<std::byte> buffer, const int flags = 0) noexcept
            : IoOperation(queue), m_fd(fd), m_buffer(buffer), m_flags(flags) {}

        [[nodiscard]] ReceiveFromResult await_resume()
        {
            if (m_buffer.empty()) {
                return {};
            }
            const auto bytes = static_cast<std::size_t>(finish("async datagram read"));
            return {bytes, SocketAddress{m_addr, m_msg.msg_namelen}};
        }
    };
}