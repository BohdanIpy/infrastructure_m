module;

#include <cstdint>
#include <chrono>
#include <iostream>
#include <format>

export module CoreUtility:Wrappers;

export namespace core_utility::wrappers
{
    template <typename... Fs>
    struct match : Fs... { using Fs::operator()...; };
    template <typename... Fs>
    match(Fs...) -> match<Fs...>;

    template <typename... Func>
        class CleanUpWrapper
    {
        std::tuple<std::decay_t<Func>...> funcs;
    public:
        explicit CleanUpWrapper(Func&&... f)
        : funcs(std::forward<Func>(f)...)
        {}

        CleanUpWrapper(const CleanUpWrapper&) = delete;
        CleanUpWrapper& operator=(const CleanUpWrapper&) = delete;

        CleanUpWrapper(CleanUpWrapper&&) noexcept = default;
        CleanUpWrapper& operator=(CleanUpWrapper&&) noexcept = default;

        ~CleanUpWrapper()
        {
            [this]<std::size_t... I>(std::index_sequence<I...>){
                (..., std::get<I>(funcs)());
            }(std::make_index_sequence<sizeof...(Func)>{});
        }
    };
}