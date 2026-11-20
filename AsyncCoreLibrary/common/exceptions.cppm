module;

#include <stdexcept>
#include <system_error>
#include <cstdint>

export module AsyncCoreLibrary:Exceptions;

export namespace async_core::common
{
    struct InvalidHandleException : std::invalid_argument {
        InvalidHandleException() : std::invalid_argument("Attempted to schedule an invalid coroutine handle") {}
    };

    struct SchedulerMemoryException : std::bad_alloc {
        SchedulerMemoryException() = default;
    };

    struct OperationCancelledException : std::runtime_error {
        OperationCancelledException() : std::runtime_error("Async operation was cancelled.") {}
    };

    struct TimeoutException : std::runtime_error {
        TimeoutException() : std::runtime_error("Operation timed out.") {}
    };

    struct InvalidCoroutineHandleException : std::runtime_error
    {
        InvalidCoroutineHandleException() : std::runtime_error("Invalid coroutine handle exception.") {}
    };

    struct TaskResumeFailedException : std::runtime_error
    {
        TaskResumeFailedException() : std::runtime_error("Task resume failed.") {}
    };

    struct TaskReturnedNullReferenceException : std::runtime_error
    {
        TaskReturnedNullReferenceException() : std::runtime_error("Task returned null reference.") {}
    };

    struct TaskFinishedWithoutProducingValueException : std::runtime_error
    {
        TaskFinishedWithoutProducingValueException() : std::runtime_error("Task finished without producing a value.") {}
    };

    struct BaseSystemException : std::system_error
    {
        BaseSystemException(std::int32_t code, const std::error_category& ec, const char* what) : std::system_error(code, ec, what) {}
    };

    struct AsyncClosedFailedException : BaseSystemException
    {
        using BaseSystemException::BaseSystemException;
    };

    struct AsyncConnectFailedException : BaseSystemException
    {
        using BaseSystemException::BaseSystemException;
    };

    struct AsyncPollCancelledOrFailedException : BaseSystemException
    {
        using BaseSystemException::BaseSystemException;
    };

    struct AsyncFdReadFailedException : BaseSystemException
    {
        using BaseSystemException::BaseSystemException;
    };

}