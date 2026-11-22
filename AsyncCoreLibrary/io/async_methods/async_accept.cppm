module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncAccept;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    struct AcceptResult
    {
        SocketT fd{-1};         // the new connection: the caller owns it and must close it
        SocketAddress peer{};
    };

    /// accept4() on a listening socket. If the accept completes at the very moment a stop is requested, the connection is
    /// returned (never dropped), and the next checkpoint reports the cancellation.
    class AsyncAccept final : public IoOperation<AsyncAccept>
    {
        friend class IoOperation<AsyncAccept>;
        static constexpr bool kCancellable = true;

        SocketT m_listen_fd;
        int m_flags;
        sockaddr_storage m_addr{};
        socklen_t m_addrlen{sizeof(sockaddr_storage)};

        void prepare(io_uring_sqe* sqe) noexcept
        {
            m_addrlen = sizeof(m_addr);
            io_uring_prep_accept(sqe, m_listen_fd, reinterpret_cast<sockaddr*>(&m_addr), &m_addrlen, m_flags);
        }

    public:
        AsyncAccept(IoQueue& queue, const SocketT listen_fd, const int flags = SOCK_CLOEXEC | SOCK_NONBLOCK) noexcept
            : IoOperation(queue), m_listen_fd(listen_fd), m_flags(flags) {}

        [[nodiscard]] AcceptResult await_resume()
        {
            const int fd = finish("async accept");
            return {fd, SocketAddress{m_addr, m_addrlen}};
        }
    };
}