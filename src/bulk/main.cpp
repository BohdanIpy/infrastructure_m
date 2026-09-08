#include <iostream>
#include <stack>
#include <vector>
#include <cassert>

std::stack<bool> brackets;

#define PRINT_BULK(commands) \
    do { \
        std::cout << "bulk: "; \
        for (const auto& cmd : commands) \
        { \
            std::cout << cmd << " "; \
        } \
        std::cout << std::endl; \
        commands.clear();   \
    } while (false); \

int main(int argc, char* argv[])
{
    assert(argc == 2 && "Usage: ./exe <block size>");

    std::vector<std::string> commands;
    int size = std::stoi(argv[1]);

    std::string input;
    while (std::getline(std::cin, input))
    {
        if (input == "{")
        {
            if (brackets.empty() && !commands.empty())
            {
                PRINT_BULK(commands);
            }
            brackets.push(true);
        }
        else if (input == "}")
        {
            assert(!brackets.empty() && "Mismatch with brackets");
            brackets.pop();
            if (brackets.empty())
            {
                PRINT_BULK(commands);
            }
        }
        else
        {
            commands.push_back(input);
            if (brackets.empty())
            {
                if (commands.size() == size)
                {
                    PRINT_BULK(commands);
                }
            }
        }
    }

    if (brackets.empty() && !commands.empty())
    {
        PRINT_BULK(commands);
    }

    return 0;
}