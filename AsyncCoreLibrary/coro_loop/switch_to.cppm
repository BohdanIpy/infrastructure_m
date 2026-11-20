module;

#include <cassert>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

export module AsyncCoreLibrary:SwitchTo;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :ScheduleTypes;
import :Exceptions;
import :Concepts;

namespace async_core::coro_loop
{
    using common::HasSafeHandleConcept;
    using common::InvalidHandleException;
    using common::ScheduleStatus;
    using common::SchedulerMemoryException;
}

export namespace async_core::coro_loop
{
    /**
     * Awaiter which moves the awaiting coroutine onto a scheduler worker.
     *
     * Success: the coroutine continues on the worker thread, await_resume() returns Success.
     * Failure: await_suspend() returns false, so the coroutine continues INLINE on the CURRENT thread and
     *          await_resume() reports why (SchedulerStopped is returned, the others throw). The handle handed to the
     *          scheduler was only a non-owning copy, so nothing leaks and nothing is destroyed on this path.
     *
     * Do not ignore the result if the code after the switch relies on running on the target thread.
     */
    struct SwitchTo
    {
        std::reference_wrapper<ICoroScheduler> m_scheduler;
        std::optional<std::uint64_t> m_index;
        ScheduleStatus m_status{ScheduleStatus::Success};

        explicit SwitchTo(ICoroScheduler& scheduler, const std::optional<std::uint64_t> index = std::nullopt) noexcept
            : m_scheduler(scheduler), m_index(index) {}

        [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires HasSafeHandleConcept<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> caller) noexcept
        {
            auto safeHandle = caller.promise().GetSafeHandle();

            const ScheduleStatus status = m_index.has_value()
                ? m_scheduler.get().scheduleOnPosition(std::move(safeHandle), *m_index)
                : m_scheduler.get().schedule(std::move(safeHandle));

            if (status != ScheduleStatus::Success) {
                m_status = status;
                return false;
            }
            return true;
        }

        [[nodiscard]] ScheduleStatus await_resume() const
        {
            switch (m_status) {
                case ScheduleStatus::Success:
                    return ScheduleStatus::Success;

                case ScheduleStatus::SchedulerStopped:
                    return ScheduleStatus::SchedulerStopped;

                case ScheduleStatus::InvalidHandle:
                    throw InvalidHandleException{};

                case ScheduleStatus::OutOfMemory:
                    throw SchedulerMemoryException{};

                default:
                    throw std::runtime_error("Unknown scheduling failure");
            }
        }
    };

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler& scheduler) noexcept {
        return SwitchTo(scheduler);
    }

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler& scheduler, std::uint64_t thread_idx) noexcept {
        return SwitchTo(scheduler, thread_idx);
    }

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler* scheduler) noexcept {
        assert(scheduler != nullptr);
        return SwitchTo(*scheduler);
    }

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler* scheduler, std::uint64_t thread_idx) noexcept {
        assert(scheduler != nullptr);
        return SwitchTo(*scheduler, thread_idx);
    }
}