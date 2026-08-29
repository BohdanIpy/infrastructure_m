module;

#include <coroutine>
#include <system_error>
#include <tuple>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncAccept;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;

export namespace async_core::io
{
    class AsyncAccept {
        IoQueue* m_queue{nullptr};
        const SocketT m_listenFd{-1};
        const int m_flags{SOCK_CLOEXEC | SOCK_NONBLOCK};
        IoCtx m_ctx{};

    public:
        explicit AsyncAccept(IoQueue* queue, SocketT listenFd, int flags = SOCK_CLOEXEC | SOCK_NONBLOCK) noexcept
            : m_queue(queue), m_listenFd(listenFd), m_flags(flags)
        {}

        AsyncAccept(const AsyncAccept&) = delete;
        AsyncAccept& operator=(const AsyncAccept&) = delete;
        AsyncAccept(AsyncAccept&&) = delete;
        AsyncAccept& operator=(AsyncAccept&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().get_safe_handle();

            m_ctx.prepare_accept();

            m_queue->submit_accept(
                m_listenFd,
                &m_ctx,
                m_flags
            );
        }

        [[nodiscard]]
        std::tuple<SocketT, sockaddr_storage, socklen_t> await_resume() {
            if (m_ctx.result < 0) {
                throw std::system_error(
                    -m_ctx.result,
                    std::generic_category(),
                    "Async accept failed"
                );
            }

            return {
                (m_ctx.result),
                m_ctx.payload.connection.addr,
                m_ctx.payload.connection.addrlen
            };
        }
    };
}