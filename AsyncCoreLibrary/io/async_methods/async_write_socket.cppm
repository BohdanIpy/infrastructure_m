module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncWriteSocket;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// send() on a connected socket. MSG_NOSIGNAL is on by default, so a closed peer is an IoError (EPIPE), not SIGPIPE.
    /// Returns the number of bytes sent, which may be LESS than the buffer size: loop if you need all of it.
    class AsyncWriteSocket final : public IoOperation<AsyncWriteSocket>
    {
        friend class IoOperation<AsyncWriteSocket>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        std::span<const std::byte> m_buffer;
        int m_flags;

        void prepare(io_uring_sqe* sqe) noexcept
        {
            io_uring_prep_send(sqe, m_fd, m_buffer.data(), m_buffer.size(), m_flags);
        }
        [[nodiscard]] bool nothing_to_do() const noexcept { return m_buffer.empty(); }

    public:
        AsyncWriteSocket(IoQueue& queue, const SocketT fd, const std::span<const std::byte> buffer,
                         const int flags = MSG_NOSIGNAL) noexcept
            : IoOperation(queue), m_fd(fd), m_buffer(buffer), m_flags(flags) {}

        [[nodiscard]] std::size_t await_resume()
        {
            return m_buffer.empty() ? 0 : static_cast<std::size_t>(finish("async socket write"));
        }
    };
}