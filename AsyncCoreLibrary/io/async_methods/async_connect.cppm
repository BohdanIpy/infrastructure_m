module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncConnect;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// connect(). The address is copied, so it may be a temporary.
    class AsyncConnect final : public IoOperation<AsyncConnect>
    {
        friend class IoOperation<AsyncConnect>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        sockaddr_storage m_addr{};
        socklen_t m_addrlen;

        void prepare(io_uring_sqe* sqe) noexcept
        {
            io_uring_prep_connect(sqe, m_fd, reinterpret_cast<const sockaddr*>(&m_addr), m_addrlen);
        }

    public:
        AsyncConnect(IoQueue& queue, const SocketT fd, const sockaddr* address, const socklen_t length) noexcept
            : IoOperation(queue), m_fd(fd),
              m_addrlen(length > sizeof(sockaddr_storage) ? static_cast<socklen_t>(sizeof(sockaddr_storage)) : length)
        {
            std::memcpy(&m_addr, address, m_addrlen);
        }

        void await_resume() { (void)finish("async connect"); }
    };
}