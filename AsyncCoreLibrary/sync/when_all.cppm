module;

#include <type_traits>
#include <coroutine>
#include <mutex>
#include <atomic>
#include <exception>
#include <memory>

export module AsyncCoreLibrary:WhenAll;

export namespace async_core::sync
{
    namespace detail {

        // Helper trait to extract the awaiter type
        template <typename Awaitable, typename = void>
        struct get_awaiter {
            using type = Awaitable;
        };

        template <typename Awaitable>
        struct get_awaiter<Awaitable, std::void_t<decltype(std::declval<Awaitable>().operator co_await())>> {
            using type = decltype(std::declval<Awaitable>().operator co_await());
        };

        template <typename Awaitable>
        struct get_awaiter<Awaitable, std::enable_if_t<
            !requires { std::declval<Awaitable>().operator co_await(); } &&
             requires { operator co_await(std::declval<Awaitable>()); }
        >> {
            using type = decltype(operator co_await(std::declval<Awaitable>()));
        };

        template <typename Awaitable>
        using awaiter_t = typename get_awaiter<Awaitable>::type;

        template <typename Awaitable>
        using await_result_t = decltype(std::declval<awaiter_t<Awaitable>>().await_resume());

        // Shared state between tasks and the main awaiter
        template <typename... Results>
        struct WhenAllState {
            std::atomic<size_t> remaining{sizeof...(Results)};
            std::coroutine_handle<> parent_handle{nullptr};
            std::tuple<Results...> results;
            std::exception_ptr exception{nullptr};
        };

        // Internal task type with initial_suspend = suspend_always
        // so it doesn't run immediately upon creation.
        struct ScheduledWhenAllTask {
            struct promise_type {
                ScheduledWhenAllTask get_return_object() {
                    return ScheduledWhenAllTask{std::coroutine_handle<promise_type>::from_promise(*this)};
                }
                std::suspend_always initial_suspend() noexcept { return {}; }
                std::suspend_never final_suspend() noexcept { return {}; }
                void return_void() noexcept {}
                void unhandled_exception() noexcept { std::terminate(); }
            };

            std::coroutine_handle<> handle;
        };

        // Task wrapper executing each individual awaitable
        template <size_t Index, typename Awaitable, typename StatePtr>
        auto make_when_all_task(Awaitable awaitable, StatePtr state) -> ScheduledWhenAllTask {
            try {
                if constexpr (std::is_void_v<await_result_t<Awaitable>>) {
                    co_await std::forward<Awaitable>(awaitable);
                } else {
                    std::get<Index>(state->results) = co_await std::forward<Awaitable>(awaitable);
                }
            } catch (...) {
                if (!state->exception) {
                    state->exception = std::current_exception();
                }
            }

            if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (state->parent_handle) {
                    state->parent_handle.resume();
                }
            }
        }
    } // namespace detail

    // Version 1: Accepts an executor/loop that implements .schedule(std::coroutine_handle<>)
    template <typename Executor, typename... Awaitables>
    [[nodiscard]] auto when_all_on(Executor& executor, Awaitables&&... awaitables) {
        using ResultsTuple = std::tuple<detail::await_result_t<Awaitables>...>;
        using StateType = detail::WhenAllState<detail::await_result_t<Awaitables>...>;

        struct WhenAllAwaiter {
            std::shared_ptr<StateType> state;

            bool await_ready() const noexcept {
                return state->remaining.load(std::memory_order_acquire) == 0;
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept {
                state->parent_handle = h;
                return state->remaining.load(std::memory_order_acquire) > 0;
            }

            ResultsTuple await_resume() {
                if (state->exception) {
                    std::rethrow_exception(state->exception);
                }
                return std::move(state->results);
            }
        };

        auto state = std::make_shared<StateType>();
        auto args_tuple = std::forward_as_tuple(std::forward<Awaitables>(awaitables)...);

        [&]<size_t... Is>(std::index_sequence<Is...>) {
            ((executor.schedule(
                 detail::make_when_all_task<Is>(
                     std::get<Is>(std::move(args_tuple)), state).handle
             )), ...);
        }(std::index_sequence_for<Awaitables...>{});

        return WhenAllAwaiter{std::move(state)};
    }

}