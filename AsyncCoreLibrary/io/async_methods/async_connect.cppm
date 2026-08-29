module;

#include <coroutine>
#include <system_error>
#include <cstring>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncConnect;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncConnect
    {
        IoQueue* m_queue{nullptr};
        const SocketT m_fd{-1};
        const sockaddr* m_addr;
        const socklen_t m_addrlen;
        IoCtx m_ctx{};

    public:
        explicit AsyncConnect(
            IoQueue* queue,
            const SocketT fd,
            const sockaddr* addr,
            const socklen_t addrlen
        ) noexcept : m_queue(queue), m_fd(fd), m_addr(addr), m_addrlen(addrlen)
        {}

        AsyncConnect(const AsyncConnect&) = delete;
        AsyncConnect& operator=(const AsyncConnect&) = delete;
        AsyncConnect(AsyncConnect&&) = delete;
        AsyncConnect& operator=(AsyncConnect&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_connect(m_addr, m_addrlen);

            m_queue->submit_connect(
                m_fd,
                &m_ctx
            );
        }

        void await_resume() const {
            if (m_ctx.result < 0) {
                throw std::system_error(
                    -m_ctx.result,
                    std::generic_category(),
                    "Async connect failed"
                );
            }
        }
    };
}