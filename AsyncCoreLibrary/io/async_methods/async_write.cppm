module;

#include <coroutine>
#include <system_error>
#include <span>
#include <cstdint>

export module AsyncCoreLibrary:AsyncWrite;

import :IoLoop;
import :IoTypes;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncWrite
    {
        IoQueue* m_queue;
        const SocketT m_fd;
        const std::span<std::byte> m_buffer;
        const uint64_t m_offset{0};
        IoCtx m_ctx;

    public:
        AsyncWrite(IoQueue* queue, const SocketT fd, const std::span<std::byte> buffer, const uint64_t offset = 0)
                : m_queue(queue), m_fd(fd), m_buffer(buffer), m_offset(offset)
        {}

        AsyncWrite(const AsyncWrite&) = delete;
        AsyncWrite& operator=(const AsyncWrite&) = delete;
        AsyncWrite(AsyncWrite&&) = delete;
        AsyncWrite& operator=(AsyncWrite&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_stream_write(m_buffer, m_offset);

            m_queue->submit_write(m_fd, &m_ctx);
        }

        [[nodiscard]] std::size_t await_resume() const {
            if (m_ctx.result < 0) {
                throw std::system_error(-m_ctx.result, std::generic_category(), "Async read failed");
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };
}