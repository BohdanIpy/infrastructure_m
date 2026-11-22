module;

#include <cerrno>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <type_traits>

#include <liburing.h>

export module AsyncCoreLibrary:IoOperation;

import :IoLoop;
import :IoTypes;
import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :Concepts;
import :Exceptions;

namespace async_core::io
{
    using common::AwaitableCancellationBase;
    using common::HasSafeHandleConcept;
    using common::OperationCancelledException;

    namespace detail
    {
        /// Stop action of an I/O wait: asks the kernel to cancel the operation. It only marks the context and queues a cancel
        /// request; the operation's own completion is what resumes the coroutine. It touches the context only while the
        /// awaiter is alive (the awaiter's stop_callback is destroyed in await_resume, which waits for a running callback).
        struct CancelIo
        {
            IoQueue* queue;
            IoCtx* ctx;

            void operator()() const noexcept
            {
                ctx->cancel_requested.store(true, std::memory_order_release);
                queue->Cancel(*ctx);
            }
        };
        static_assert(std::is_nothrow_move_constructible_v<CancelIo>);
    }
}

export namespace async_core::io
{
    /**
     * Base of all I/O awaiters (CRTP). A derived operation only says WHAT to submit and what to return:
     *
     *   static constexpr bool kCancellable = true;                   // false for close / shutdown
     *   void prepare(io_uring_sqe*) noexcept;                        // io_uring_prep_xxx(...)
     *   [[nodiscard]] bool nothing_to_do() const noexcept;           // optional: complete without any I/O (empty buffer)
     *   T await_resume() { const auto n = this->finish("read"); ... }
     *
     * The protocol (shared, so it is written and tested once):
     *   - the operation is submitted from await_suspend; the coroutine is resumed ONLY by its completion, never earlier, so
     *     the kernel never writes into buffers of a coroutine that has moved on;
     *   - if the completion arrives before await_suspend returns, the coroutine simply continues inline (Init/Armed handshake);
     *   - a stop request on the awaiting coroutine asks the kernel to cancel the operation. The operation then completes
     *     (usually with -ECANCELED) and finish() throws OperationCancelledException. If the operation had already finished,
     *     its result is returned normally: a result is never thrown away, the next checkpoint reports the cancellation;
     *   - failures throw IoError with the errno value; if the operation could not even be submitted that is reported the same way.
     *
     * Awaiters are neither copyable nor movable (the kernel holds their address); create them in the co_await expression.
     */
    template <typename Derived>
    class IoOperation : private AwaitableCancellationBase<detail::CancelIo>
    {
    protected:
        IoQueue& m_queue;
        IoCtx m_ctx;

        explicit IoOperation(IoQueue& queue) noexcept : m_queue(queue) {}

        /// Completes the awaiter: stops watching the token and turns the completion into a result or an exception.
        std::int32_t finish(const char* operation)
        {
            if constexpr (Derived::kCancellable) {
                this->unwatch_stop();
            }
            const std::int32_t result = m_ctx.result;
            if (result >= 0) {
                return result;
            }
            if (result == -ECANCELED && m_ctx.cancel_requested.load(std::memory_order_acquire)) {
                throw OperationCancelledException{};
            }
            throw IoError(-result, operation);
        }

    public:
        IoOperation(const IoOperation&) = delete;
        IoOperation& operator=(const IoOperation&) = delete;
        IoOperation(IoOperation&&) = delete;
        IoOperation& operator=(IoOperation&&) = delete;

        [[nodiscard]] bool await_ready() const noexcept
        {
            if constexpr (requires (const Derived& d) { { d.nothing_to_do() } -> std::convertible_to<bool>; }) {
                return static_cast<const Derived&>(*this).nothing_to_do();
            } else {
                return false;
            }
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller)
        {
            auto& promise = caller.promise();
            m_ctx.coro = promise.GetSafeHandle();

            // Publishes everything written so far (the handle, the buffers' descriptors) to the completion thread. The kernel
            // already orders submission before completion; this states it in the C++ memory model as well (it pairs with the
            // acquire load in IoQueue::complete), which is also what lets ThreadSanitizer follow it.
            m_ctx.phase.store(IoCtx::Init, std::memory_order_release);

            Derived& self = static_cast<Derived&>(*this);
            if (const int error = m_queue.Submit(m_ctx, [&self](io_uring_sqe* sqe) noexcept { self.prepare(sqe); }); error < 0) {
                m_ctx.result = error;       // never submitted, so no completion will come: continue inline and report it
                return false;
            }

            // From here the kernel owns the buffers and the completion may arrive at any moment, but it will NOT resume us
            // before arm(), so the awaiter and the frame stay alive until the last line.
            if constexpr (Derived::kCancellable) {
                this->watch_stop(this->stop_token_of(promise), detail::CancelIo{&m_queue, &m_ctx});     // runs now if stop was requested
            }
            return m_ctx.arm();
        }
    };
}