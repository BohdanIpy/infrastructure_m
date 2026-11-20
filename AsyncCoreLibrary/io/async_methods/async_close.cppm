module;

#include <system_error>
#include <coroutine>

export module AsyncCoreLibrary:AsyncClose;

import :IoLoop;
import :IoTypes;
import :SafeCoroutineHandle;
import :Exceptions;
import :Concepts;

namespace async_core::io
{
    using common::HasSafeHandleConcept;
    using common::AsyncClosedFailedException;
}

export namespace async_core::io
{
    class AsyncClose {
        std::reference_wrapper<IoQueue> m_queue;
        const SocketT m_fd;
        IoCtx m_ctx;

    public:
        AsyncClose(IoQueue& queue, const SocketT fd) noexcept
            : m_queue(queue), m_fd(fd)
        {}

        AsyncClose(const AsyncClose&) = delete;
        AsyncClose& operator=(const AsyncClose&) = delete;
        AsyncClose(AsyncClose&&) = delete;
        AsyncClose& operator=(AsyncClose&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().GetSafeHandle();

            m_ctx.type = EventType::CLOSE;

            m_queue.get().submit_close(m_fd, &m_ctx);
        }

        void await_resume() const {
            if (m_ctx.result < 0) {
                throw AsyncClosedFailedException{-m_ctx.result, std::generic_category(), "Async close failed"};
            }
        }
    };

}
