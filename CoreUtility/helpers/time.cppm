module;

#include <cstdint>
#include <chrono>
#include <iostream>
#include <format>

export module CoreUtility:Time;

export namespace core_utility::time
{
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
}