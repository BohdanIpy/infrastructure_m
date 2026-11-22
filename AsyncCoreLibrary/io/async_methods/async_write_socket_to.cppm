module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncWriteSocketTo;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// sendmsg() for datagram sockets: one datagram to `destination`. The address is copied, so it may be a temporary.
    class AsyncWriteSocketTo final : public IoOperation<AsyncWriteSocketTo>
    {
        friend class IoOperation<AsyncWriteSocketTo>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        std::span<const std::byte> m_buffer;
        int m_flags;
        sockaddr_storage m_addr{};
        socklen_t m_addrlen;
        iovec m_iov{};
        msghdr m_msg{};

        void prepare(io_uring_sqe* sqe) noexcept
        {
            m_iov.iov_base = const_cast<std::byte*>(m_buffer.data());
            m_iov.iov_len = m_buffer.size();
            m_msg.msg_name = &m_addr;
            m_msg.msg_namelen = m_addrlen;
            m_msg.msg_iov = &m_iov;
            m_msg.msg_iovlen = 1;
            io_uring_prep_sendmsg(sqe, m_fd, &m_msg, static_cast<unsigned>(m_flags));
        }
        [[nodiscard]] bool nothing_to_do() const noexcept { return m_buffer.empty(); }

    public:
        AsyncWriteSocketTo(IoQueue& queue, const SocketT fd, const std::span<const std::byte> buffer,
                           const sockaddr* destination, const socklen_t length, const int flags = MSG_NOSIGNAL) noexcept
            : IoOperation(queue), m_fd(fd), m_buffer(buffer), m_flags(flags),
              m_addrlen(length > sizeof(sockaddr_storage) ? static_cast<socklen_t>(sizeof(sockaddr_storage)) : length)
        {
            std::memcpy(&m_addr, destination, m_addrlen);
        }

        [[nodiscard]] std::size_t await_resume()
        {
            return m_buffer.empty() ? 0 : static_cast<std::size_t>(finish("async datagram write"));
        }
    };
}