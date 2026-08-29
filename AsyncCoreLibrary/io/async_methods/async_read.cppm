module;

#include <system_error>
#include <coroutine>
#include <span>
#include <cstddef>
#include <cstdint>

export module AsyncCoreLibrary:AsyncRead;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncRead {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        IoCtx m_ctx{};
        std::span<std::byte> m_buffer;
        const std::uint64_t m_offset = static_cast<std::uint64_t>(-1);
    public:
        explicit AsyncRead(
            IoQueue* queue,
            const SocketT fd,
            std::span<std::byte> buffer,
            const std::uint64_t offset = static_cast<std::uint64_t>(-1)
        ) noexcept
            : m_queue(queue), m_fd(fd), m_buffer(buffer), m_offset(offset)
        {}

        AsyncRead(const AsyncRead&) = delete;
        AsyncRead& operator=(const AsyncRead&) = delete;
        AsyncRead(AsyncRead&&) = delete;
        AsyncRead& operator=(AsyncRead&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept {
            return m_buffer.empty();
        }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_stream_read(m_buffer, m_offset);

            m_queue->submit_read(m_fd, &m_ctx);
        }

        [[nodiscard]]
        std::size_t await_resume() const {
            if (m_buffer.empty()) {
                return 0;
            }

            if (m_ctx.result < 0) {
                throw std::system_error(
                    -m_ctx.result,
                    std::generic_category(),
                    "Async fd read failed"
                );
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };
}