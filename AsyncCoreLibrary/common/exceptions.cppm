module;

#include <stdexcept>

export module AsyncCoreLibrary:Exceptions;

export namespace async_core::common
{
    struct InvalidHandleException : std::invalid_argument {
        InvalidHandleException() : std::invalid_argument("Attempted to schedule an invalid coroutine handle") {}
    };

    struct SchedulerMemoryException : std::bad_alloc {
        SchedulerMemoryException() = default;
    };

    struct OperationCancelledException : std::exception {
        const char* what() const noexcept override {
            return "Async operation was cancelled.";
        }
    };

    struct TimeoutException : std::runtime_error {
        TimeoutException() : std::runtime_error("Operation timed out") {}
    };
}