module;

#include <type_traits>
#include <coroutine>
#include <mutex>
#include <atomic>
#include <exception>
#include <memory>
#include <utility>
#include <tuple>
#include <variant>


export module AsyncCoreLibrary:WhenAll;

import :SafeCoroutineHandle;
import :ICoroScheduler;
import :SyncWait;
import :Task;
import :AsyncLatch;
import :DetachedTask;

using async_core::exec::SafeCoroutineHandle;
using async_core::coro_loop::ICoroScheduler;
using async_core::sync::sync_wait;
using async_core::sync::AsyncLatch;
using async_core::exec::Task;
using async_core::exec::DetachedTask;


export namespace async_core::sync
{
namespace details
    {
        template <typename... TaskTypes>
        class WhenAllAwaitable
        {
        public:
            using ReturnType = std::tuple<typename std::decay_t<TaskTypes>::ReturnType...>;

        private:
            ICoroScheduler& m_scheduler;
            std::tuple<std::decay_t<TaskTypes>...> m_tasks;
            std::unique_ptr<Task<ReturnType>> m_awaitableTask{nullptr};

            template <std::size_t I, typename TaskType, typename ResultsPtr>
            static DetachedTask run_one_task(
                ICoroScheduler& sched,
                TaskType task,
                std::shared_ptr<AsyncLatch> latch_ptr,
                ResultsPtr results_ptr)
            {
                using ResType = typename TaskType::ReturnType;

                if constexpr (std::is_void_v<ResType> || std::is_same_v<ResType, std::monostate>) {
                    co_await task;
                    std::get<I>(*results_ptr) = std::monostate{};
                } else {
                    std::get<I>(*results_ptr) = co_await task;
                }

                latch_ptr->count_down();
            }

            static auto launchAll(ICoroScheduler& scheduler, std::tuple<std::decay_t<TaskTypes>...> tasks) -> Task<ReturnType>
            {
                constexpr auto N = sizeof...(TaskTypes);

                auto latch = std::make_shared<AsyncLatch>(N);
                auto results = std::make_shared<ReturnType>();

                [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    (
                        [&scheduler, &tasks, &latch, &results]() {
                            auto launcher = run_one_task<Is>(
                                scheduler,
                                std::move(std::get<Is>(tasks)),
                                latch,
                                results
                            );
                            scheduler.schedule(launcher.get_safe_handle());
                        }(),
                        ...
                    );
                }(std::make_index_sequence<N>{});

                co_await *latch;
                co_return std::move(*results);
            }

        public:
            WhenAllAwaitable(ICoroScheduler& scheduler, TaskTypes&&... awaitables)
                : m_scheduler{scheduler}, m_tasks(std::forward<TaskTypes>(awaitables)...)
            {}

            bool await_ready() const noexcept
            {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle)
            {
                m_awaitableTask = std::make_unique<Task<ReturnType>>(launchAll(m_scheduler, std::move(m_tasks)));
                m_awaitableTask->get_safe_handle()->template raw_handle<typename Task<ReturnType>::promise_type>().promise().continuation = handle;

                m_scheduler.schedule(m_awaitableTask->get_safe_handle());
            }

            ReturnType await_resume()
            {
                return m_awaitableTask->get();
            }
        };
    }

    template <typename... Tasks>
    [[nodiscard]] auto when_all_on(ICoroScheduler& scheduler, Tasks&&... awaitables) {
        return details::WhenAllAwaitable<Tasks...>{scheduler, std::forward<Tasks>(awaitables)...};
    }
}