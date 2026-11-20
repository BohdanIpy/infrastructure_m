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
        using value_type  = T;
        using stored_type = std::optional<T>;
        using result_type = T;

        stored_type value;

        template <typename U>
            requires std::convertible_to<U, T>
        void return_value(U&& val) {
            value.emplace(std::forward<U>(val));
        }

        result_type result() {
            return std::move(value.value());
        }
    };

    template <typename T>
    struct PromiseReturnBase<T&> {
        using value_type  = T&;
        using stored_type = T*;
        using result_type = T&;

        stored_type value{nullptr};

        void return_value(T& val) noexcept {
            value = std::addressof(val);
        }

        result_type result() const {
            return *value;
        }
    };

    template <>
    struct PromiseReturnBase<void> {
        using value_type  = void;
        using stored_type = std::monostate;
        using result_type = void;

        stored_type value{};

        void return_void() noexcept {}
        result_type result() const noexcept {}
    };
}