module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncShutdown;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// shutdown(fd, how). Not cancellable: it completes immediately anyway.
    class AsyncShutdown final : public IoOperation<AsyncShutdown>
    {
        friend class IoOperation<AsyncShutdown>;
        static constexpr bool kCancellable = false;

        SocketT m_fd;
        int m_how;

        void prepare(io_uring_sqe* sqe) noexcept { io_uring_prep_shutdown(sqe, m_fd, m_how); }

    public:
        AsyncShutdown(IoQueue& queue, const SocketT fd, const int how = SHUT_WR) noexcept
            : IoOperation(queue), m_fd(fd), m_how(how) {}

        void await_resume() { (void)finish("async shutdown"); }
    };
}