module;

#include <system_error>
#include <coroutine>

export module AsyncCoreLibrary:AsyncClose;

import :IoLoop;
import :IoTypes;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncClose {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd;
        IoCtx m_ctx;

    public:
        AsyncClose(IoQueue* queue, const SocketT fd) noexcept
            : m_queue(queue), m_fd(fd)
        {}

        AsyncClose(const AsyncClose&) = delete;
        AsyncClose& operator=(const AsyncClose&) = delete;
        AsyncClose(AsyncClose&&) = delete;
        AsyncClose& operator=(AsyncClose&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.type = EventType::CLOSE;

            m_queue->submit_close(m_fd, &m_ctx);
        }

        void await_resume() const {
            if (m_ctx.result < 0) {
                throw std::system_error(-m_ctx.result, std::generic_category(), "Async close failed");
            }
        }
    };

}
