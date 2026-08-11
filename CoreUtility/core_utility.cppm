module;

#include <cstdint>
#include <chrono>
#include <iostream>
#include <format>

export module CoreUtility;

export namespace core_utility
{
    template <typename... Fs>
    struct match : Fs... { using Fs::operator()...; };
    template <typename... Fs>
    match(Fs...) -> match<Fs...>;

    void GetTime()
    {
        std::chrono::zoned_time zt{std::chrono::current_zone(), std::chrono::system_clock::now()};
        std::string formatted = std::format("{:%a %b %d %T %Y}", zt);
        std::cout << "Local time is: " << formatted << '\n';
    }

    std::string GetTimeString()
    {
        std::chrono::zoned_time zt{std::chrono::current_zone(), std::chrono::system_clock::now()};
        std::string formatted = std::format("{:%a %b %d %T %Y}", zt);
        return formatted;
    }

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
