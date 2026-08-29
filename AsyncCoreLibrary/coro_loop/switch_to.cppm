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
        ICoroScheduler* m_scheduler{nullptr};
        std::optional<std::size_t> m_index;

        explicit SwitchTo(ICoroScheduler* scheduler, const std::optional<std::size_t> index = std::nullopt) : m_scheduler(scheduler), m_index(index) {}

        [[nodiscard]]
        bool await_ready() const noexcept
        {
            return false;
        }

        template <typename PromiseT>
            requires exec::HasSafeHandle<PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> coroutine) noexcept
        {
            if (m_index.has_value())
            {
                m_scheduler->scheduleOnPosition(coroutine.promise().get_safe_handle(), *m_index);
            }
            else
            {
                m_scheduler->schedule(coroutine.promise().get_safe_handle());
            }
        }

        void await_resume() const noexcept {}
    };
}
