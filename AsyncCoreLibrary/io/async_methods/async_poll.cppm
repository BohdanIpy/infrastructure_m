module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/socket.h>
#include <poll.h>
#include <liburing.h>

export module AsyncCoreLibrary:AsyncPoll;

import :IoLoop;
import :IoTypes;
import :IoOperation;

export namespace async_core::io
{
    struct PollResult
    {
        std::int32_t raw_events{0};

        [[nodiscard]] bool can_read()  const noexcept { return (raw_events & POLLIN) != 0; }
        [[nodiscard]] bool can_write() const noexcept { return (raw_events & POLLOUT) != 0; }
        [[nodiscard]] bool has_hungup() const noexcept { return (raw_events & (POLLHUP | POLLRDHUP)) != 0; }
        [[nodiscard]] bool has_error()  const noexcept { return (raw_events & POLLERR) != 0; }
    };

    /// One-shot poll: completes when one of `events` (POLLIN, POLLOUT, ...) is ready.
    class AsyncPoll final : public IoOperation<AsyncPoll>
    {
        friend class IoOperation<AsyncPoll>;
        static constexpr bool kCancellable = true;

        SocketT m_fd;
        short m_events;

        void prepare(io_uring_sqe* sqe) noexcept
        {
            io_uring_prep_poll_add(sqe, m_fd, static_cast<unsigned>(m_events));
        }

    public:
        AsyncPoll(IoQueue& queue, const SocketT fd, const short events) noexcept
            : IoOperation(queue), m_fd(fd), m_events(events) {}

        [[nodiscard]] PollResult await_resume() { return PollResult{finish("async poll")}; }
    };
}