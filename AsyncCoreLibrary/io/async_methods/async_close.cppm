module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>

#include <liburing.h>

export module AsyncCoreLibrary:AsyncClose;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    /// close(). Not cancellable: a close that was started must finish, otherwise the descriptor leaks.
    /// Do not close a descriptor while other operations on it are in flight; cancel and await them first.
    class AsyncClose final : public IoOperation<AsyncClose>
    {
        friend class IoOperation<AsyncClose>;
        static constexpr bool kCancellable = false;

        SocketT m_fd;

        void prepare(io_uring_sqe* sqe) noexcept { io_uring_prep_close(sqe, m_fd); }

    public:
        AsyncClose(IoQueue& queue, const SocketT fd) noexcept : IoOperation(queue), m_fd(fd) {}

        void await_resume() { (void)finish("async close"); }
    };
}