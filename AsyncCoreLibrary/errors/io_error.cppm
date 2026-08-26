module;

#include <cstring>
#include <format>
#include <optional>
#include <source_location>
#include <string>

export module AsyncCoreLibrary:IoError;

export namespace async_core::errors
{
    struct IoError {
        int code = 0;

        std::optional<int> os_errno;

        std::source_location location;

        std::string message;

        static inline IoError
        from_code(int code, const std::source_location& location = std::source_location::current()) {
            return {
                .code = code,
                .location = location,
              };
        }

        static inline IoError
        from_errno(const std::source_location& location = std::source_location::current()) {
            return {
                .os_errno = errno,
                .location = location,
              };
        }

        static inline IoError
        from_errno(int err, const std::source_location& location = std::source_location::current()) {
            return {
                .os_errno = err,
                .location = location,
              };
        }
    };
}

namespace std
{
    template <>
    struct std::formatter<async_core::errors::IoError> : std::formatter<std::string> {
        auto format(const async_core::errors::IoError& error, std::format_context& ctx) const {
            auto out = std::format_to(ctx.out(), "IoError: code={}", error.code);
            if (error.os_errno.has_value()) {
                const char* msg = strerror(*error.os_errno);
                out = std::format_to(out, " errno={} errno_message='{}'", *error.os_errno, msg);
            }
            out = std::format_to(out, " file={} line={} function={}",
                                 error.location.file_name(),
                                 error.location.line(),
                                 error.location.function_name());
            return out;
        }
    };
}