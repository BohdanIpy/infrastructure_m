module;

#include <coroutine>
#include <system_error>
#include <span>
#include <cstddef>

export module AsyncCoreLibrary:AsyncReadSocket;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;
import :Concepts;

using async_core::common::HasSafeHandleConcept;

export namespace async_core::io
{
    class AsyncReadSocket
    {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        std::span<std::byte> m_buffer;
        const int m_flags{0};
        IoCtx m_ctx{};

    public:
        explicit AsyncReadSocket(
            IoQueue* queue,
            const SocketT fd,
            std::span<std::byte> buffer,
            const int flags = 0
        ) noexcept
            : m_queue(queue), m_fd(fd), m_buffer(buffer), m_flags(flags)
        {}

        AsyncReadSocket(const AsyncReadSocket&) = delete;
        AsyncReadSocket& operator=(const AsyncReadSocket&) = delete;
        AsyncReadSocket(AsyncReadSocket&&) = delete;
        AsyncReadSocket& operator=(AsyncReadSocket&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept {
            return m_buffer.empty();
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().GetSafeHandle();

            m_ctx.prepare_stream_read(m_buffer);

            m_queue->submit_read_socket(
                m_fd,
                &m_ctx,
                m_flags
            );
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
                    "Async socket read failed"
                );
            }
            return static_cast<std::size_t>(m_ctx.result);
        }
    };
}