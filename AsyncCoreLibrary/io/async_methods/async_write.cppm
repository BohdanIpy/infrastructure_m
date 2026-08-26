module;

#include <coroutine>
#include <system_error>
#include <span>

export module AsyncCoreLibrary:AsyncWrite;

import :IoLoop;
import :IoTypes;

export namespace async_core::io
{
    class AsyncWrite
    {
        IoQueue& m_queue;
        SocketT m_fd;
        std::span<const std::byte> m_buffer;
        IoCtx m_ctx;

    public:

        AsyncWrite(IoQueue& queue, SocketT fd, std::span<const std::byte> buffer)
                : m_queue(queue), m_fd(fd), m_buffer(buffer) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h)
        {
            m_ctx.coroutine = h;
            m_queue.submit_write(m_fd, m_buffer, &m_ctx);
        }

        [[nodiscard]] std::size_t await_resume() const {
            if (m_ctx.result < 0) {
                throw std::system_error(-m_ctx.result, std::generic_category(), "Async read failed");
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };
}