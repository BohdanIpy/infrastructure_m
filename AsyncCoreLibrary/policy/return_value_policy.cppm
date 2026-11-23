module;

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

export module AsyncCoreLibrary:ReturnValuePolicy;

export namespace async_core::policy {
// ==========================================
// Return Value Handling
// ==========================================
// The result is read through BasicPromiseType::take_result(); these bases only store it.

template <typename T> struct PromiseReturnBase {
    static_assert(!std::is_rvalue_reference_v<T>,
                  "a task cannot return an rvalue reference (it would dangle)");

    using value_type = T;
    using stored_type = std::optional<T>;

    stored_type m_value;

    template <typename U>
        requires std::convertible_to<U, T>
    void return_value(U&& val) {
        m_value.emplace(std::forward<U>(val));
    }
};

template <typename T> struct PromiseReturnBase<T&> {
    using value_type = T&;
    using stored_type = T*;

    stored_type m_value{nullptr};

    void return_value(T& val) noexcept { m_value = std::addressof(val); }
};

template <> struct PromiseReturnBase<void> {
    using value_type = void;
    using stored_type = std::monostate;

    stored_type m_value{};

    void return_void() noexcept {}
};
} // namespace async_core::policy
