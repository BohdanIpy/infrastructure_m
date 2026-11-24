module;

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

export module AsyncCoreLibrary:AsyncOneshot;

import :ICoroScheduler;
import :AwaitableCancellationBase;
import :CancelWait;
import :WaitState;
import :Exceptions;
import :Concepts;

namespace async_core::sync {
using common::AsyncOneshotAlreadyTakenException;
using common::AwaitableCancellationBase;
using common::HasSafeHandleConcept;
using common::OperationCancelledException;
using common::TaskFinishedWithoutProducingValueException;
using common::WaitRef;
using common::WaitState;
using coro_loop::CancelWait;
using coro_loop::ICoroScheduler;
using coro_loop::wake;

template <typename T> struct OneshotStorage {
    enum class Status : std::uint8_t { Empty, HasValue, HasException, Taken };

    Status m_status = Status::Empty;
    std::optional<T> m_value;
    std::exception_ptr m_exception = nullptr;

    [[nodiscard]] bool ready() const noexcept { return m_status != Status::Empty; }

    template <typename U> bool store_value(U&& value) {
        if (m_status != Status::Empty) {
            return false;
        }
        m_value.emplace(std::forward<U>(value));
        m_status = Status::HasValue;
        return true;
    }

    bool store_exception(std::exception_ptr error) noexcept {
        if (m_status != Status::Empty) {
            return false;
        }
        m_exception = std::move(error);
        m_status = Status::HasException;
        return true;
    }

    T take() {
        switch (m_status) {
            case Status::Taken:
                throw AsyncOneshotAlreadyTakenException{};
            case Status::Empty:
                throw TaskFinishedWithoutProducingValueException{};
            case Status::HasException: {
                m_status = Status::Taken;
                std::exception_ptr error = std::exchange(m_exception, nullptr);
                std::rethrow_exception(std::move(error));
            }
            case Status::HasValue:
                break;
        }
        m_status = Status::Taken;
        T result = std::move(*m_value);
        m_value.reset();
        return result;
    }

    std::optional<T> try_take() {
        if (m_status == Status::Empty) {
            return std::nullopt;
        }
        return std::optional<T>{take()};
    }
};
} // namespace async_core::sync

export namespace async_core::sync {
template <typename T>
    requires(!std::is_void_v<T>)
class AsyncOneshot {
    static_assert(!std::is_reference_v<T>, "a oneshot cannot hold a reference (it would dangle)");

    ICoroScheduler& m_scheduler;
    mutable std::mutex m_mtx;
    OneshotStorage<T> m_storage;
    WaitRef m_waiter;

    static void release(ICoroScheduler& scheduler, const WaitRef& waiter,
                        WaitState::Phase outcome) noexcept {
        if (!waiter) {
            return;
        }
        bool resume_needed = false;
        if (waiter->Complete(outcome, resume_needed) && resume_needed) {
            wake(scheduler, *waiter);
        }
    }

  public:
    explicit AsyncOneshot(ICoroScheduler& scheduler)
        : m_scheduler(scheduler) {}

    AsyncOneshot(const AsyncOneshot&) = delete;
    AsyncOneshot& operator=(const AsyncOneshot&) = delete;
    AsyncOneshot(AsyncOneshot&&) = delete;
    AsyncOneshot& operator=(AsyncOneshot&&) = delete;

    ~AsyncOneshot() {
        WaitRef waiter;
        {
            std::lock_guard lock(m_mtx);
            waiter = std::move(m_waiter);
        }
        release(m_scheduler, waiter, WaitState::Cancelled);
    }

    class Awaiter : private AwaitableCancellationBase<CancelWait> {
        enum class Outcome : std::uint8_t { Ready, Parked, Rejected };

        friend class AsyncOneshot;

        AsyncOneshot& m_owner;
        WaitRef m_wait;
        Outcome m_outcome = Outcome::Ready;

        explicit Awaiter(AsyncOneshot& owner) noexcept
            : m_owner(owner) {}

      public:
        [[nodiscard]] bool await_ready() const noexcept { return m_owner.IsReady(); }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) {
            auto& promise = caller.promise();

            auto state = WaitState::Make(promise.GetSafeHandle());
            m_wait = state;

            this->watch_stop(this->stop_token_of(promise), CancelWait{state, &m_owner.m_scheduler});

            {
                std::lock_guard lock(m_owner.m_mtx);

                if (m_owner.m_storage.ready()) {
                    bool _ = false;
                    (void)state->Complete(WaitState::Completed, _);
                    return false;
                }
                if (m_owner.m_waiter && !m_owner.m_waiter->IsFinished()) {
                    m_outcome = Outcome::Rejected;
                    return false;
                }
                m_owner.m_waiter = state;
                m_outcome = Outcome::Parked;
            }

            return state->Arm();
        }

        T await_resume() {
            this->unwatch_stop();
            if (m_outcome == Outcome::Rejected) {
                throw AsyncOneshotAlreadyTakenException{};
            }
            if (m_wait && m_wait->GetPhase() == WaitState::Cancelled) {
                throw OperationCancelledException{};
            }
            std::lock_guard lock(m_owner.m_mtx);
            return m_owner.m_storage.take();
        }
    };

    template <typename U>
        requires std::convertible_to<U, T>
    bool SetValue(U&& value) {
        ICoroScheduler& scheduler = m_scheduler;
        WaitRef waiter;
        {
            std::lock_guard lock(m_mtx);
            if (!m_storage.store_value(std::forward<U>(value))) {
                return false;
            }
            waiter = std::move(m_waiter);
        }
        release(scheduler, waiter, WaitState::Completed);
        return true;
    }

    bool SetException(std::exception_ptr error) {
        ICoroScheduler& scheduler = m_scheduler;
        WaitRef waiter;
        {
            std::lock_guard lock(m_mtx);
            if (!m_storage.store_exception(std::move(error))) {
                return false;
            }
            waiter = std::move(m_waiter);
        }
        release(scheduler, waiter, WaitState::Completed);
        return true;
    }

    [[nodiscard]] bool IsReady() const {
        std::lock_guard lock(m_mtx);
        return m_storage.ready();
    }

    [[nodiscard]] std::optional<T> TryTake() {
        std::lock_guard lock(m_mtx);
        return m_storage.try_take();
    }

    [[nodiscard]] Awaiter operator co_await() { return Awaiter{*this}; }
};
} // namespace async_core::sync