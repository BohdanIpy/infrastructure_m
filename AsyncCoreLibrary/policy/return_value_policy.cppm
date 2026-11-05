module;

#include <optional>
#include <semaphore>
#include <variant>

export module AsyncCoreLibrary:ReturnValuePolicy;

import :TaskTypes;

using async_core::common::TaskType;

export namespace async_core::policy
{
    // ==========================================
    // Return Value Handling
    // ==========================================

    template <typename T>
    struct PromiseReturnBase {
        std::optional<T> value;

        template <typename U>
            requires std::convertible_to<U, T>
        void return_value(U&& val) {
            value.emplace(std::forward<U>(val));
        }

        T result() {
            return std::move(value.value());
        }
    };

    template <typename T>
    struct PromiseReturnBase<T&> {
        T* value{nullptr};

        void return_value(T& val) noexcept {
            value = std::addressof(val);
        }

        T& result() const {
            return *value;
        }
    };

    template <>
    struct PromiseReturnBase<void> {
        std::monostate value{};

        void return_void() noexcept {}
        void result() const noexcept {}
    };
}