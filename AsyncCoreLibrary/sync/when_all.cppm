module;

#include <type_traits>
#include <coroutine>
#include <mutex>
#include <atomic>
#include <exception>
#include <memory>
#include <utility>

export module AsyncCoreLibrary:WhenAll;

import :SafeCoroutineHandle;

using async_core::exec::SafeCoroutineHandle;

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
            SafeCoroutineHandle parent_handle{};
            std::tuple<Results...> results;
            std::exception_ptr exception{nullptr};
        };

        // Internal task type compatible with SafeCoroutineHandle
        struct ScheduledWhenAllTask {
            struct promise_type {
                std::shared_ptr<exec::CoroutineControlBlock> control = std::make_shared<exec::CoroutineControlBlock>();
                ScheduledWhenAllTask get_return_object() {
                    return ScheduledWhenAllTask{
                        std::coroutine_handle<promise_type>::from_promise(*this)
                    };
                }
                std::suspend_always initial_suspend() noexcept { return {}; }
                std::suspend_never final_suspend() noexcept { return {}; }
                void return_void() noexcept {}
                void unhandled_exception() noexcept { std::terminate(); }

                SafeCoroutineHandle get_safe_handle() noexcept {
                    return SafeCoroutineHandle{
                        std::coroutine_handle<promise_type>::from_promise(*this),
                        control
                    };
                }
            };

            std::coroutine_handle<promise_type> raw_handle;

            SafeCoroutineHandle get_safe_handle() const noexcept {
                return raw_handle.promise().get_safe_handle();
            }
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
                if (state->parent_handle.is_valid()) {
                    state->parent_handle.try_resume();
                }
            }
        }

        // Extracted Awaiter to namespace level to allow templated await_suspend
        template <typename... Results>
        struct WhenAllAwaiter {
            using StateType = WhenAllState<Results...>;
            using ResultsTuple = std::tuple<Results...>;

            std::shared_ptr<StateType> state;

            bool await_ready() const noexcept {
                return state->remaining.load(std::memory_order_acquire) == 0;
            }

            template <typename PromiseT>
            bool await_suspend(std::coroutine_handle<PromiseT> h) noexcept {
                if constexpr (requires(PromiseT& p) { { p.get_safe_handle() } -> std::same_as<SafeCoroutineHandle>; }) {
                    state->parent_handle = h.promise().get_safe_handle();
                } else {
                    state->parent_handle = SafeCoroutineHandle{h};
                }
                return state->remaining.load(std::memory_order_acquire) > 0;
            }

            ResultsTuple await_resume() {
                if (state->exception) {
                    std::rethrow_exception(state->exception);
                }
                return std::move(state->results);
            }
        };

    }

    template <typename Executor, typename... Awaitables>
    [[nodiscard]] auto when_all_on(Executor& executor, Awaitables&&... awaitables) {
        using StateType = detail::WhenAllState<detail::await_result_t<Awaitables>...>;

        auto state = std::make_shared<StateType>();
        auto args_tuple = std::forward_as_tuple(std::forward<Awaitables>(awaitables)...);

        [&]<size_t... Is>(std::index_sequence<Is...>) {
            ((executor.schedule(
                 detail::make_when_all_task<Is>(
                     std::get<Is>(std::move(args_tuple)), state).get_safe_handle()
             )), ...);
        }(std::index_sequence_for<Awaitables...>{});

        return detail::WhenAllAwaiter<detail::await_result_t<Awaitables>...>{std::move(state)};
    }
}