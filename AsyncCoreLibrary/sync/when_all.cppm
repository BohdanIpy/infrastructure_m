module;

#include <array>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

export module AsyncCoreLibrary:WhenAll;

import :AwaitableCancellationBase;
import :SafeCoroutineHandle;
import :ICoroScheduler;
import :CancellingRoutines;
import :Concepts;
import :ScheduleTypes;
import :Task;
import :DetachedTask;
import :AsyncLatch;

namespace async_core::sync::detail {
using common::CurrentStopToken;
using common::HasGetStopTokenConcept;
using common::SafeCoroutineHandle;
using common::ScheduleStatus;
using common::shield;
using coro_loop::ICoroScheduler;
using exec::LazyDetachedTask;
using exec::Task;

template <typename TaskT>
using when_all_result_t =
    std::conditional_t<std::is_void_v<typename std::remove_cvref_t<TaskT>::value_type>,
                       std::monostate, typename std::remove_cvref_t<TaskT>::value_type>;

/**
 * @brief internal state for processing
 *
 * @tparam Ts Task types
 */
template <typename... Ts> struct WhenAllState {
    AsyncLatch latch;
    std::tuple<std::optional<when_all_result_t<Ts>>...>
        results; // store optional because not all the things are default-constructible
    std::mutex error_mtx;
    std::exception_ptr first_error;

    WhenAllState(std::ptrdiff_t count, ICoroScheduler* scheduler)
        : latch(count, scheduler) {}

    void fail(std::exception_ptr error) noexcept {
        std::lock_guard lock(error_mtx);
        if (!first_error) {
            first_error = std::move(error);
        }
    }
};

/**
 * @brief Internal LazyDetachedTask wrapper to push the job into scheduler. ONLY DetachedTask here
 * is viable, because it if we would use Task<>, we cannot guarantee the proper cleanup of the
 * function: after .count_down(), one thread would try to delete a Task from inner, while other
 * thread from outer would try also to delete, and this is UB
 *
 * @tparam I position inside a tuple
 * @tparam StateT Shared to which info was written
 * @tparam TaskT Job to be executed
 * @param state Instance of shared resources
 * @param task Actual Job instance to run
 * @return LazyDetachedTask
 */
template <std::size_t I, typename StateT, typename TaskT>
LazyDetachedTask run_one(StateT& state, TaskT task) {
    try {
        if constexpr (std::is_void_v<typename TaskT::value_type>) {
            co_await std::move(task);
            std::get<I>(state.results).emplace();
        } else {
            std::get<I>(state.results).emplace(co_await std::move(task));
        }
    } catch (...) {
        // Store the first exception
        state.fail(std::current_exception());
    }
    state.latch.CountDown();
}
} // namespace async_core::sync::detail

export namespace async_core::sync {
template <typename... Tasks>
    requires(sizeof...(Tasks) > 0)
[[nodiscard]] async_core::exec::Task<std::tuple<detail::when_all_result_t<Tasks>...>>
when_all_on(async_core::coro_loop::ICoroScheduler& scheduler, Tasks... tasks) {
    constexpr std::size_t N = sizeof...(Tasks);

    const std::stop_token parent_token = co_await detail::CurrentStopToken{};

    detail::WhenAllState<Tasks...> state{static_cast<std::ptrdiff_t>(N), &scheduler};

    std::array<async_core::common::SafeCoroutineHandle, N> runners{};
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (
            [&] {
                try {
                    auto runner =
                        detail::run_one<Is>(state, std::move(std::get<Is>(std::tie(tasks...))));
                    runners[Is] = runner.GetSafeHandle();
                } catch (...) {
                    state.fail(std::current_exception()); // could not even create the runner
                    state.latch.CountDown();
                }
            }(),
            ...);
    }(std::make_index_sequence<N>{});

    // cancellation via SafeCoroutineHandle, not the DetachedTask instance itself
    std::stop_callback forward{parent_token, [&runners] {
                                   for (const auto& runner : runners) {
                                       if (runner.IsValid()) {
                                           runner.RequestStop();
                                       }
                                   }
                               }};

    for (const auto& runner : runners) {
        if (!runner.IsValid()) {
            continue;
        }
        if (scheduler.Schedule(runner) != detail::ScheduleStatus::Success) {
            (void)runner.TryResume();
        }
    }

    // to ensure all jobs are done or cancelled
    (void)co_await detail::shield(state.latch);

    // throw exception in case of failure without need of unpacking all
    if (state.first_error) {
        std::rethrow_exception(state.first_error);
    }
    co_return std::apply(
        [](auto&... slots) {
            return std::tuple<detail::when_all_result_t<Tasks>...>(std::move(*slots)...);
        },
        state.results);
}
} // namespace async_core::sync
