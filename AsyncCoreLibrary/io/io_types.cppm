module;

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <thread>
#include <cstring>
#include <memory>

#include <sys/socket.h>
#include <netinet/in.h>

export module AsyncCoreLibrary:IoTypes;

import :SafeCoroutineHandle;

using async_core::exec::SafeCoroutineHandle;

export namespace async_core::io
{
    using SocketT = int;

#if defined(_WIN32)
    using native_handle_type = SOCKET;
#else
    using native_handle_type = long long;
#endif

    enum class EventType : uint8_t {
        READ = 0,         // TCP / Stream / File Read
        WRITE = 1,        // TCP / Stream / File Write
        RECV_MSG,         // UDP / SCTP Datagram Read (msghdr based)
        SEND_MSG,         // UDP / SCTP Datagram Write (msghdr based)
        ACCEPT,
        CONNECT,
        SHUTDOWN,
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
    struct DatagramPayload {
        struct msghdr msg{};
        struct iovec iov{};
        std::span<std::byte> buffer{};
        alignas(native_handle_type) sockaddr_storage remote_addr{};
        alignas(8) uint8_t control_buffer[128]{};
    };

    struct ConnectionPayload {
        alignas(native_handle_type) sockaddr_storage addr{};
        socklen_t addrlen{sizeof(sockaddr_storage)};
    };

    struct StreamPayload {
        std::span<std::byte> buffer{};
        uint64_t offset{0}; // for general fd, non socket I/O
    };

    struct IoCtx
    {
        std::shared_ptr<SafeCoroutineHandle> safe_coro{};
        void (*on_complete)(IoCtx* ctx, int32_t res){nullptr};
        int32_t result{0};
        EventType type{EventType::READ};

        union Storage {
            StreamPayload stream;
            DatagramPayload datagram;
            ConnectionPayload connection;

            Storage() {}
            ~Storage() {}
        } payload;

        void prepare_connect(const sockaddr* addr, const socklen_t addrlen) {
            type = EventType::CONNECT;
            std::memcpy(&payload.connection.addr, addr, addrlen);
            payload.connection.addrlen = addrlen;
        }

        void prepare_accept() {
            type = EventType::ACCEPT;
            payload.connection.addrlen = sizeof(sockaddr_storage);
        }

        void prepare_stream_read(std::span<std::byte> buf, const uint64_t offset = 0) noexcept {
            type = EventType::READ;
            payload.stream.buffer = buf;
            payload.stream.offset = offset;
        }

        void prepare_stream_write(const std::span<std::byte> buf, const uint64_t offset = 0) noexcept
        {
            type = EventType::WRITE;
            payload.stream.buffer = buf;
            payload.stream.offset = offset;
        }

        void prepare_udp_recv(std::span<std::byte> buf) noexcept {
            type = EventType::RECV_MSG;
            auto& dg = payload.datagram;

            dg.buffer = buf;

            std::memset(&dg.msg, 0, sizeof(dg.msg));

            dg.iov.iov_base = dg.buffer.data();
            dg.iov.iov_len  = dg.buffer.size();

            dg.msg.msg_name       = &dg.remote_addr;
            dg.msg.msg_namelen    = sizeof(dg.remote_addr);
            dg.msg.msg_iov        = &dg.iov;
            dg.msg.msg_iovlen     = 1;
            dg.msg.msg_control    = dg.control_buffer;
            dg.msg.msg_controllen = sizeof(dg.control_buffer);
        }

        void prepare_udp_send(const std::span<std::byte> buf, const sockaddr* dest_addr, const socklen_t addr_len) noexcept {
            type = EventType::SEND_MSG;
            auto& dg = payload.datagram;

            dg.buffer = buf;

            std::memset(&dg.msg, 0, sizeof(dg.msg));
            std::memcpy(&dg.remote_addr, dest_addr, addr_len);

            dg.iov.iov_base = dg.buffer.data();
            dg.iov.iov_len  = dg.buffer.size();

            dg.msg.msg_name       = &dg.remote_addr;
            dg.msg.msg_namelen    = addr_len;
            dg.msg.msg_iov        = &dg.iov;
            dg.msg.msg_iovlen     = 1;
            dg.msg.msg_control    = nullptr;
            dg.msg.msg_controllen = 0;
        }
    };
}
