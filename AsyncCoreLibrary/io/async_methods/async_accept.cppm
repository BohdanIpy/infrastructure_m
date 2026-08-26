module;

#include <coroutine>
#include <system_error>
#include <tuple>
#include <sys/socket.h>

export module AsyncCoreLibrary:AsyncAccept;

import :IoTypes;
import :IoLoop;

export namespace async_core::io
{
    class AsyncAccept {
        IoQueue& m_queue;
        SocketT m_listenFd;
        sockaddr_storage m_addr{};
        socklen_t m_addrlen{sizeof(sockaddr_storage)};
        IoCtx m_ctx;

    public:
        AsyncAccept(IoQueue& queue, int listenFd)
            : m_queue(queue), m_listenFd(listenFd) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            m_ctx.coroutine = h;
            m_queue.submit_accept(
                m_listenFd,
                reinterpret_cast<sockaddr*>(&m_addr),
                &m_addrlen,
                &m_ctx
            );
        }

        std::tuple<int, sockaddr_storage, socklen_t> await_resume() {
            if (m_ctx.result < 0) {
                throw std::system_error(-m_ctx.result, std::generic_category(), "Async accept failed");
            }
            return std::make_tuple(m_ctx.result, m_addr, m_addrlen);
        }
    };

}
