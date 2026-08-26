module;

#include <cstdint>
#include <coroutine>
#include <sys/epoll.h>
#include <poll.h>


export module AsyncCoreLibrary:AsyncPoll;

import :IoLoop;
import :IoTypes;

export namespace async_core::io
{
    class AsyncPoll {
        IoQueue& queue;
        SocketT fd;
        short poll_events;
        IoCtx ctx{};
    public:
        AsyncPoll(IoQueue& q, SocketT descriptor, short events)
            : queue(q), fd(descriptor), poll_events(events) {}

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) {
            ctx.coroutine = h;
            ctx.on_complete = nullptr;
            if (poll_events & POLLIN)
            {
                ctx.result |= status_to_int(EventType::READ);
            }
            if (poll_events & POLLOUT)
            {
                ctx.result |= status_to_int(EventType::WRITE);
            }
            queue.submit_poll(fd, poll_events, &ctx);
        }

        [[nodiscard]]
        int32_t await_resume() const noexcept {
            return ctx.result;
        }
    };
}