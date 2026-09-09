module;

#include <coroutine>
#include <optional>

export module AsyncCoreLibrary:SwitchTo;

import :ICoroScheduler;
import :SafeCoroutineHandle;

export namespace async_core::coro_loop
{
    struct SwitchTo
    {
        ICoroScheduler& m_scheduler;
        std::optional<std::size_t> m_index;
        ScheduleStatus m_status{ScheduleStatus::Success};

        explicit SwitchTo(ICoroScheduler& scheduler, const std::optional<std::size_t> index = std::nullopt)
            : m_scheduler(scheduler), m_index(index) {}

        [[nodiscard]] bool await_ready() const noexcept { return false; }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
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

        [[nodiscard]] ScheduleStatus await_resume() const noexcept
        {
            return m_status;
        }
    };
}
