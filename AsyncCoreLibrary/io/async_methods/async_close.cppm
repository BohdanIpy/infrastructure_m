module;

#include <system_error>
#include <coroutine>

export module AsyncCoreLibrary:AsyncClose;

import :IoLoop;
import :IoTypes;

export namespace async_core::io
{
    class AsyncClose {
        IoQueue& m_queue;
        SocketT m_fd;
        IoCtx m_ctx;

    public:
        AsyncClose(IoQueue& queue, SocketT fd) noexcept
            : m_queue(queue), m_fd(fd) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            m_ctx.coroutine = h;
            m_queue.submit_close(m_fd, &m_ctx);
        }

        void await_resume() {
            if (m_ctx.result < 0) {
                throw std::system_error(-m_ctx.result, std::generic_category(), "Async close failed");
            }
        }
    };

}
