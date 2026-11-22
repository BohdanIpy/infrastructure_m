module;

#include <cstddef>
#include <system_error>
#include <sys/socket.h>

export module AsyncCoreLibrary:IoTypes;

export namespace async_core::io
{
    using SocketT = int;

    /// Every failed I/O operation throws this; code() is the errno value (error_code().value()), what() names the operation.
    /// A cancelled operation throws OperationCancelledException instead (see IoOperation).
    class IoError : public std::system_error
    {
    public:
        IoError(const int errnum, const char* operation)
            : std::system_error(errnum, std::generic_category(), operation) {}
    };

    /// A socket address together with its real length (what accept / recvfrom report).
    struct SocketAddress
    {
        sockaddr_storage storage{};
        socklen_t length{0};

        [[nodiscard]] const sockaddr* get() const noexcept { return reinterpret_cast<const sockaddr*>(&storage); }
    };
}