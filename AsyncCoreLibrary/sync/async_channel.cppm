module;

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

export module AsyncCoreLibrary:AsyncChannel;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :AwaitableCancellationBase;
import :WaitState;
import :Concepts;
import :CancelWait;
import :Wake;
import :Exceptions;

namespace async_core::sync {
using common::AwaitableCancellationBase;
using common::HasSafeHandleConcept;
using common::OperationCancelledException;
using common::SafeCoroutineHandle;
using common::WaitState;
using coro_loop::CancelWait;
using coro_loop::ICoroScheduler;
using coro_loop::wake;
} // namespace async_core::sync

export namespace async_core::sync {

template <typename T> class AsyncChannel {
    struct RecvOp {
        WaitState state;
        std::optional<T> value;

        explicit RecvOp(SafeCoroutineHandle handle) noexcept
            : state(std::move(handle)) {}
    };

    struct SendOp {
        WaitState state;
        T value;
        std::atomic<bool> closed{false};

        SendOp(SafeCoroutineHandle handle, T&& v)
            : state(std::move(handle))
            , value(std::move(v)) {}
    };

    struct Pending {
        std::shared_ptr<WaitState> state;
        bool resume_needed{false};

        template <typename Op> void set(const std::shared_ptr<Op>& op, bool needed) noexcept {
            state = std::shared_ptr<WaitState>(op, &op->state);
            resume_needed = needed;
        }
    };

    enum class Outcome { Done, Closed, WouldBlock };

    ICoroScheduler& m_scheduler;
    const std::size_t m_capacity;
    std::mutex m_mtx;
    bool m_closed{false};
    std::deque<T> m_buffer;
    std::list<std::shared_ptr<RecvOp>> m_readers;
    std::list<std::shared_ptr<SendOp>> m_writers;

    static void wake_pending(ICoroScheduler& scheduler, Pending& pending) noexcept {
        if (pending.state && pending.resume_needed) {
            wake(scheduler, *pending.state);
        }
    }

    Outcome try_send_locked(T& value, Pending& pending) {
        if (m_closed) {
            return Outcome::Closed;
        }

        while (!m_readers.empty()) {
            std::shared_ptr<RecvOp> reader = std::move(m_readers.front());
            m_readers.pop_front();

            reader->value.emplace(
                std::move(value)); // publish the value first, then try to claim the reader
            bool needed = false;
            if (reader->state.complete(WaitState::Completed, needed)) {
                pending.set(reader, needed);
                return Outcome::Done;
            }
            value = std::move(
                *reader->value); // reader was cancelled: take the value back, try the next one
            reader->value.reset();
        }

        if (m_capacity > 0 && m_buffer.size() < m_capacity) {
            m_buffer.push_back(std::move(value));
            return Outcome::Done;
        }
        return Outcome::WouldBlock;
    }

    /// Caller holds m_mtx.
    Outcome try_receive_locked(std::optional<T>& out, Pending& pending) {
        if (!m_buffer.empty()) {
            out.emplace(std::move(m_buffer.front()));
            m_buffer.pop_front();

            // a slot is free now: move the first live waiting sender into the buffer
            while (!m_writers.empty()) {
                std::shared_ptr<SendOp> writer = std::move(m_writers.front());
                m_writers.pop_front();

                m_buffer.push_back(std::move(writer->value));
                bool needed = false;
                if (writer->state.complete(WaitState::Completed, needed)) {
                    pending.set(writer, needed);
                    break;
                }
                m_buffer.pop_back(); // sender was cancelled: its value is dropped with it
            }
            return Outcome::Done;
        }

        while (!m_writers.empty()) { // rendezvous (or empty buffer): take straight from a sender
            std::shared_ptr<SendOp> writer = std::move(m_writers.front());
            m_writers.pop_front();

            bool needed = false;
            if (writer->state.complete(WaitState::Completed, needed)) {
                out.emplace(std::move(writer->value));
                pending.set(writer, needed);
                return Outcome::Done;
            }
        }

        return m_closed ? Outcome::Closed : Outcome::WouldBlock;
    }

  public:
    class SendAwaiter : private AwaitableCancellationBase<CancelWait> {
        friend class AsyncChannel;

        AsyncChannel& m_ch;
        std::optional<T> m_value; // until it is handed to the op in await_suspend
        std::shared_ptr<SendOp> m_op;
        bool m_inline{false}; // finished without parking
        bool m_result{false};

        SendAwaiter(AsyncChannel& ch, T&& value)
            : m_ch(ch)
            , m_value(std::move(value)) {}

      public:
        [[nodiscard]] bool await_ready() {
            ICoroScheduler& scheduler = m_ch.m_scheduler;
            Pending pending;
            Outcome outcome;
            {
                std::lock_guard lock(m_ch.m_mtx);
                outcome = m_ch.try_send_locked(*m_value, pending);
            }
            AsyncChannel::wake_pending(scheduler, pending);
            if (outcome == Outcome::WouldBlock) {
                return false;
            }
            m_inline = true;
            m_result = (outcome == Outcome::Done);
            return true;
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();
            ICoroScheduler& scheduler = m_ch.m_scheduler;

            m_op = std::make_shared<SendOp>(promise.GetSafeHandle(), std::move(*m_value));
            auto state = std::shared_ptr<WaitState>(m_op, &m_op->state);
            this->watch_stop(this->stop_token_of(promise), CancelWait{state, &m_ch.m_scheduler});

            Pending pending;
            Outcome outcome;
            {
                std::lock_guard lock(m_ch.m_mtx);
                outcome = m_ch.try_send_locked(
                    m_op->value, pending); // somebody may have arrived since await_ready()
                if (outcome == Outcome::WouldBlock) {
                    if (state->IsFinished()) {
                        return false; // cancelled before queueing: await_resume() throws
                    }
                    m_ch.m_writers.push_back(m_op);
                }
            }

            if (outcome != Outcome::WouldBlock) {
                m_inline = true;
                m_result = (outcome == Outcome::Done);
                AsyncChannel::wake_pending(scheduler, pending);
                return false;
            }
            return state->Arm(); // after this only `state` / the op may be used
        }

        /// true: delivered or buffered; false: the channel is closed. Throws
        /// OperationCancelledException if cancelled.
        [[nodiscard]] bool await_resume() {
            this->unwatch_stop();
            if (m_inline) {
                return m_result;
            }
            if (m_op->state.phase() == WaitState::Completed) {
                return true;
            }
            if (m_op->closed.load(std::memory_order_acquire)) {
                return false;
            }
            throw OperationCancelledException{};
        }
    };

    class ReceiveAwaiter : private AwaitableCancellationBase<CancelWait> {
        friend class AsyncChannel;

        AsyncChannel& m_ch;
        std::shared_ptr<RecvOp> m_op;
        std::optional<T> m_inline_value;
        bool m_inline{false};

        explicit ReceiveAwaiter(AsyncChannel& ch) noexcept
            : m_ch(ch) {}

      public:
        [[nodiscard]] bool await_ready() {
            ICoroScheduler& scheduler = m_ch.m_scheduler;
            Pending pending;
            Outcome outcome;
            {
                std::lock_guard lock(m_ch.m_mtx);
                outcome = m_ch.try_receive_locked(m_inline_value, pending);
            }
            AsyncChannel::wake_pending(scheduler, pending);
            if (outcome == Outcome::WouldBlock) {
                return false;
            }
            m_inline = true; // Done: m_inline_value holds the item; Closed: it stays empty
            return true;
        }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();
            ICoroScheduler& scheduler = m_ch.m_scheduler;

            m_op = std::make_shared<RecvOp>(promise.GetSafeHandle());
            auto state = std::shared_ptr<WaitState>(m_op, &m_op->state);
            this->watch_stop(this->stop_token_of(promise), CancelWait{state, &m_ch.m_scheduler});

            Pending pending;
            Outcome outcome;
            {
                std::lock_guard lock(m_ch.m_mtx);
                outcome = m_ch.try_receive_locked(m_inline_value, pending);
                if (outcome == Outcome::WouldBlock) {
                    if (state->IsFinished()) {
                        return false;
                    }
                    m_ch.m_readers.push_back(m_op);
                }
            }

            if (outcome != Outcome::WouldBlock) {
                m_inline = true;
                AsyncChannel::wake_pending(scheduler, pending);
                return false;
            }
            return state->Arm();
        }

        [[nodiscard]] std::optional<T> await_resume() {
            this->unwatch_stop();
            if (m_inline) {
                return std::move(m_inline_value);
            }
            if (m_op->state.phase() == WaitState::Completed) {
                return std::move(m_op->value); // empty if close() released us
            }
            throw OperationCancelledException{};
        }
    };

    explicit AsyncChannel(ICoroScheduler& scheduler, std::size_t capacity = 0) noexcept
        : m_scheduler(scheduler)
        , m_capacity(capacity) {}

    AsyncChannel(const AsyncChannel&) = delete;
    AsyncChannel& operator=(const AsyncChannel&) = delete;
    AsyncChannel(AsyncChannel&&) = delete;
    AsyncChannel& operator=(AsyncChannel&&) = delete;

    ~AsyncChannel() { shutdown(WaitState::Cancelled); }

    /// See the class comment. Idempotent.
    void close() noexcept { shutdown(WaitState::Completed); }

    [[nodiscard]] SendAwaiter send(T value) { return SendAwaiter{*this, std::move(value)}; }
    [[nodiscard]] ReceiveAwaiter receive() noexcept { return ReceiveAwaiter{*this}; }

  private:
    void shutdown(WaitState::Phase reader_outcome) noexcept {
        std::list<std::shared_ptr<RecvOp>> readers;
        std::list<std::shared_ptr<SendOp>> writers;
        {
            std::lock_guard lock(m_mtx);
            if (m_closed) {
                return;
            }
            m_closed = true;
            readers.swap(m_readers);
            writers.swap(m_writers);
        }

        ICoroScheduler& scheduler = m_scheduler;
        for (auto& reader : readers) {
            bool needed = false;
            if (reader->state.complete(reader_outcome, needed) && needed) {
                wake(scheduler, reader->state);
            }
        }
        for (auto& writer : writers) {
            writer->closed.store(
                true,
                std::memory_order_release); // before complete(): the sender reads it after waking
            bool needed = false;
            if (writer->state.complete(WaitState::Cancelled, needed) && needed) {
                wake(scheduler, writer->state);
            }
        }
    }
};
} // namespace async_core::sync