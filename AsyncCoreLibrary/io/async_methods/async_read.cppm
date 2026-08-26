module;

#include <system_error>
#include <coroutine>
#include <span>

export module AsyncCoreLibrary:AsyncRead;

import :IoTypes;
import :IoLoop;

export namespace async_core::io
{
    class AsyncRead {
        IoQueue& m_queue;
        SocketT m_fd;
        std::span<std::byte> m_buffer;
        IoCtx m_ctx;

    public:
        AsyncRead(IoQueue& queue, SocketT fd, std::span<std::byte> buffer)
            : m_queue(queue), m_fd(fd), m_buffer(buffer) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            m_ctx.coroutine = h;
            m_queue.submit_read(m_fd, m_buffer, &m_ctx);
        }

        std::size_t await_resume() {
            if (m_ctx.result < 0) {
                throw std::system_error(-m_ctx.result, std::generic_category(), "Async read failed");
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };

}