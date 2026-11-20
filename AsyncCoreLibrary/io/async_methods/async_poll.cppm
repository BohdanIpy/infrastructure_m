module;

#include <cstdint>
#include <coroutine>
#include <system_error>
#include <poll.h>

export module AsyncCoreLibrary:AsyncPoll;

import :IoLoop;
import :IoTypes;
import :SafeCoroutineHandle;
import :Exceptions;
import :Concepts;

namespace async_core::io
{
    using common::HasSafeHandleConcept;
    using common::AsyncPollCancelledOrFailedException;
}

export namespace async_core::io
{
    struct PollResult {
        std::int32_t raw_events{0};

        [[nodiscard]] bool can_read()  const noexcept { return (raw_events & POLLIN) != 0; }
        [[nodiscard]] bool can_write() const noexcept { return (raw_events & POLLOUT) != 0; }
        [[nodiscard]] bool has_hungup() const noexcept { return (raw_events & (POLLHUP | POLLRDHUP)) != 0; }
        [[nodiscard]] bool has_error()  const noexcept { return (raw_events & POLLERR) != 0; }
    };

    class AsyncPoll {
        std::reference_wrapper<IoQueue> m_queue;
        const SocketT m_fd{-1};
        const short m_pollEvents{0};
        IoCtx m_ctx{};

    public:
        explicit AsyncPoll(IoQueue& queue, SocketT descriptor, short events) noexcept
            : m_queue(queue), m_fd(descriptor), m_pollEvents(events)
        {}

        AsyncPoll(const AsyncPoll&) = delete;
        AsyncPoll& operator=(const AsyncPoll&) = delete;
        AsyncPoll(AsyncPoll&&) = delete;
        AsyncPoll& operator=(AsyncPoll&&) = delete;

        [[nodiscard]]
        bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) {
            m_ctx.safe_coro = h.promise().GetSafeHandle();

            m_ctx.type = EventType::POLL;

            m_queue.get().submit_poll(m_fd, m_pollEvents, &m_ctx);
        }

        [[nodiscard]]
        PollResult await_resume() const {
            if (m_ctx.result < 0) {
                throw AsyncPollCancelledOrFailedException{
                    -m_ctx.result,
                    std::generic_category(),
                    "Async poll canceled or failed"
                };
            }

            return PollResult{ .raw_events = m_ctx.result };
        }
    };
}