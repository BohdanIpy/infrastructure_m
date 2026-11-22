module;

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <semaphore>
#include <type_traits>
#include <utility>

export module AsyncCoreLibrary:SyncWait;

namespace async_core::sync::detail {
template <typename T> struct SyncWaitState {
    std::binary_semaphore done{0};
    std::exception_ptr error;
    std::optional<T> value;

    void publish() noexcept { done.release(); }
};

template <> struct SyncWaitState<void> {
    std::binary_semaphore done{0};
    std::exception_ptr error;

    void publish() noexcept { done.release(); }
};

struct SyncWaitHelper {
    struct promise_type {
        SyncWaitHelper get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
};

template <typename TaskPlain, typename T>
SyncWaitHelper run_sync_wait(TaskPlain& task, std::shared_ptr<SyncWaitState<T>> slot) {
    try {
        if constexpr (std::is_void_v<T>) {
            co_await std::move(task);
        } else {
            slot->value.emplace(co_await std::move(task));
        }
    } catch (...) {
        slot->error = std::current_exception();
    }
    slot->publish();
}
} // namespace async_core::sync::detail

export namespace async_core::sync {

template <typename TaskT>
    requires(!std::is_lvalue_reference_v<TaskT>) &&
            requires { typename std::remove_cvref_t<TaskT>::value_type; }
auto sync_wait(TaskT&& task) {
    using TaskPlain = std::remove_cvref_t<TaskT>;
    using ValueType = typename TaskPlain::value_type;

    auto slot = std::make_shared<detail::SyncWaitState<ValueType>>();

    detail::run_sync_wait<TaskPlain, ValueType>(task, slot);
    slot->done.acquire();

    if (slot->error) {
        std::rethrow_exception(slot->error);
    }
    if constexpr (!std::is_void_v<ValueType>) {
        return std::move(*slot->value);
    }
}
} // namespace async_core::sync