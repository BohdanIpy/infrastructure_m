module;

#include <coroutine>
#include <system_error>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncShutdown;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncShutdown
    {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        const int m_how{SHUT_WR};
        IoCtx m_ctx{};

    public:
        explicit AsyncShutdown(
            IoQueue* queue,
            SocketT fd,
            int how = SHUT_WR
        ) noexcept
            : m_queue(queue), m_fd(fd), m_how(how)
        {}

        AsyncShutdown(const AsyncShutdown&) = delete;
        AsyncShutdown& operator=(const AsyncShutdown&) = delete;
        AsyncShutdown(AsyncShutdown&&) = delete;
        AsyncShutdown& operator=(AsyncShutdown&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.type = EventType::SHUTDOWN;

            m_queue->submit_shutdown(
                m_fd,
                m_how,
                &m_ctx
            );
        }

        void await_resume() const {
            if (m_ctx.result < 0) {
                throw std::system_error(
                    -m_ctx.result,
                    std::generic_category(),
                    "Async shutdown failed"
                );
            }
        }
    };
}