module;

#include <coroutine>
#include <system_error>
#include <cstring>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncConnect;

import :IoTypes;
import :IoLoop;
import :SafeCoroutineHandle;
import :Exceptions;
import :Concepts;

namespace async_core::io
{
    using common::HasSafeHandleConcept;
    using common::AsyncConnectFailedException;
}

export namespace async_core::io
{
    class AsyncConnect
    {
        std::reference_wrapper<IoQueue> m_queue;
        const SocketT m_fd{-1};
        const sockaddr* m_addr;
        const socklen_t m_addrlen;
        IoCtx m_ctx{};

    public:
        AsyncConnect(
            IoQueue& queue,
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
            requires HasSafeHandleConcept<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().GetSafeHandle();

            m_ctx.prepare_connect(m_addr, m_addrlen);

            m_queue.get().submit_connect(
                m_fd,
                &m_ctx
            );
        }

        void await_resume() const {
            if (m_ctx.result < 0) {
                throw AsyncConnectFailedException{
                    -m_ctx.result,
                    std::generic_category(),
                    "Async connect failed"
                };
            }
        }
    };
}