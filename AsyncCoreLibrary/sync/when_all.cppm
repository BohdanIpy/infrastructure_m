module;

#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <memory>
#include <utility>
#include <variant>

export module AsyncCoreLibrary:WhenAll;

import :SafeCoroutineHandle;
import :ICoroScheduler;
import :CancellingRoutines;     // shield
import :ScheduleTypes;
import :Task;
import :DetachedTask;
import :AsyncLatch;

namespace async_core::sync::detail
{
    using async_core::common::shield;
    using async_core::coro_loop::ICoroScheduler;
    using async_core::exec::DetachedTask;
    using async_core::exec::Task;

    template <typename TaskT>
    using when_all_result_t = std::conditional_t<
        std::is_void_v<typename std::remove_cvref_t<TaskT>::value_type>,
        std::monostate,
        typename std::remove_cvref_t<TaskT>::value_type>;

    template <typename... Ts>
    struct WhenAllState
    {
        AsyncLatch latch;
        std::tuple<std::optional<when_all_result_t<Ts>>...> results;
        std::mutex error_mtx;
        std::exception_ptr first_error;

        WhenAllState(std::ptrdiff_t count, ICoroScheduler* scheduler) : latch(count, scheduler) {}

        void fail(std::exception_ptr error) noexcept
        {
            std::lock_guard lock(error_mtx);
            if (!first_error) {
                first_error = std::move(error);
            }
        }
    };

    /// Runs ONE child. Never throws: a failure is recorded, and the latch is ALWAYS counted down (the old code skipped it
    /// when a child threw, so the whole when_all hung forever).
    template <std::size_t I, typename StateT, typename TaskT>
    DetachedTask run_one(StateT& state, TaskT task)
    {
        try {
            if constexpr (std::is_void_v<typename TaskT::value_type>) {
                co_await std::move(task);
                std::get<I>(state.results).emplace();
            } else {
                std::get<I>(state.results).emplace(co_await std::move(task));
            }
        } catch (...) {
            state.fail(std::current_exception());
        }
        state.latch.count_down();       // last use of `state` by this runner
    }
}

export namespace async_core::sync
{
    template <typename... Tasks>
        requires (sizeof...(Tasks) > 0)
    [[nodiscard]] async_core::exec::Task<std::tuple<detail::when_all_result_t<Tasks>...>>
    when_all_on(async_core::coro_loop::ICoroScheduler& scheduler, Tasks... tasks)
    {
        constexpr std::size_t N = sizeof...(Tasks);
        detail::WhenAllState<Tasks...> state{static_cast<std::ptrdiff_t>(N), &scheduler};

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&] {
                try {
                    auto runner = detail::run_one<Is>(state, std::move(std::get<Is>(std::tie(tasks...))));
                    auto handle = runner.GetSafeHandle();
                    if (scheduler.Schedule(handle) != async_core::common::ScheduleStatus::Success) {
                        (void)handle.TryResume();           // refused: start inline, never lose the child
                    }
                } catch (...) {
                    state.fail(std::current_exception());   // could not even create the runner
                    state.latch.count_down();
                }
            }(), ...);
        }(std::make_index_sequence<N>{});

        // Shielded on purpose: running children point into this frame, so we must not leave before they are all done.
        (void)co_await detail::shield(state.latch);

        if (state.first_error) {
            std::rethrow_exception(state.first_error);
        }
        co_return std::apply(
            [](auto&... slots) { return std::tuple<detail::when_all_result_t<Tasks>...>(std::move(*slots)...); },
            state.results);
    }
}