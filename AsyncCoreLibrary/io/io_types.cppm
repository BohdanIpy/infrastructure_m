module;

#include <cstdint>
#include <coroutine>
#include <vector>
#include <string>

export module AsyncCoreLibrary:IoTypes;

export namespace async_core::io
{
    using SocketT = int;

    enum class EventType : uint8_t {
        READ = 0, /* for polling completion */
        WRITE = 1, /* for polling completion */
        ACCEPT,
        CONNECT,
        CLOSE,
        TIMEOUT,
        CANCEL,
        POLL,
    };

    constexpr int status_to_int(EventType s) noexcept {
        return static_cast<int>(s);
    }

    constexpr EventType int_to_status(int val) noexcept {
        return static_cast<EventType>(val);
    }

    struct IoCtx {
        std::coroutine_handle<> coroutine{nullptr};
        void (*on_complete)(IoCtx* ctx, int32_t res){nullptr};
        int32_t result{0};
        EventType type{EventType::READ};
    };
}
