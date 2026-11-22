module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncWrite;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// write() on any file descriptor. `offset` = -1 writes at the current position.
    /// Returns the number of bytes written, which may be LESS than the buffer size: loop if you need all of it.
    class AsyncWrite final : public IoOperation<AsyncWrite>
    {
        friend class IoOperation<AsyncWrite>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        std::span<const std::byte> m_buffer;
        std::uint64_t m_offset;

        void prepare(io_uring_sqe* sqe) noexcept
        {
            io_uring_prep_write(sqe, m_fd, m_buffer.data(), static_cast<unsigned>(m_buffer.size()), m_offset);
        }
        [[nodiscard]] bool nothing_to_do() const noexcept { return m_buffer.empty(); }

    public:
        AsyncWrite(IoQueue& queue, const SocketT fd, const std::span<const std::byte> buffer,
                   const std::uint64_t offset = static_cast<std::uint64_t>(-1)) noexcept
            : IoOperation(queue), m_fd(fd), m_buffer(buffer), m_offset(offset) {}

        [[nodiscard]] std::size_t await_resume()
        {
            return m_buffer.empty() ? 0 : static_cast<std::size_t>(finish("async write"));
        }
    };
}