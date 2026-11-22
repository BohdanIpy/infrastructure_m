module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncReadSocket;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// recv() on a connected socket. Returns the number of bytes received; 0 means the peer closed the connection.
    class AsyncReadSocket final : public IoOperation<AsyncReadSocket>
    {
        friend class IoOperation<AsyncReadSocket>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        std::span<std::byte> m_buffer;
        int m_flags;

        void prepare(io_uring_sqe* sqe) noexcept
        {
            io_uring_prep_recv(sqe, m_fd, m_buffer.data(), m_buffer.size(), m_flags);
        }
        [[nodiscard]] bool nothing_to_do() const noexcept { return m_buffer.empty(); }

    public:
        AsyncReadSocket(IoQueue& queue, const SocketT fd, const std::span<std::byte> buffer, const int flags = 0) noexcept
            : IoOperation(queue), m_fd(fd), m_buffer(buffer), m_flags(flags) {}

        [[nodiscard]] std::size_t await_resume()
        {
            return m_buffer.empty() ? 0 : static_cast<std::size_t>(finish("async socket read"));
        }
    };
}