module;

#include <coroutine>
#include <optional>
#include <cassert>
#include <stdexcept>

export module AsyncCoreLibrary:SwitchTo;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :ScheduleTypes;
import :Exceptions;

using async_core::common::HasSafeHandle;
using async_core::common::InvalidHandleException;
using async_core::common::SchedulerMemoryException;

export namespace async_core::coro_loop
{
    struct SwitchTo
    {
        ICoroScheduler& m_scheduler;
        std::optional<std::size_t> m_index;
        ScheduleStatus m_status{ScheduleStatus::Success};

        explicit SwitchTo(ICoroScheduler& scheduler, const std::optional<std::size_t> index = std::nullopt) noexcept
            : m_scheduler(scheduler), m_index(index) {}

        [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires HasSafeHandle<PromiseT>
        bool await_suspend(std::coroutine_handle<PromiseT> coroutine) noexcept
        {
            auto safeHandle = coroutine.promise().get_safe_handle();

            m_status = m_index.has_value()
                ? m_scheduler.scheduleOnPosition(std::move(safeHandle), *m_index)
                : m_scheduler.schedule(std::move(safeHandle));

            if (m_status != ScheduleStatus::Success) {
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

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler& scheduler, std::size_t thread_idx) noexcept {
        return SwitchTo(scheduler, thread_idx);
    }

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler* scheduler) noexcept {
        assert(scheduler != nullptr);
        return SwitchTo(*scheduler);
    }

    [[nodiscard]] inline SwitchTo switch_to(ICoroScheduler* scheduler, std::size_t thread_idx) noexcept {
        assert(scheduler != nullptr);
        return SwitchTo(*scheduler, thread_idx);
    }
}