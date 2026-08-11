module;

#include <optional>
#include <sys/socket.h>

export module NetworkCoreLibrary:SocketEndpoint;

import :SocketAddress;
import :CoreApi;

using namespace network_core::core_api;

export namespace network_core::socket_endpoint
{
class SocketEndpoint
{

    SocketAddress m_address;
    SocketType    m_type{SocketType::Stream};
    Protocol      m_protocol{Protocol::Auto};

public:

    SocketEndpoint() : m_address{SocketAddress::Any(NetSocketIpType::IP_V4, 0)}{}

    SocketEndpoint(SocketAddress addr, SocketType type = SocketType::Stream, Protocol proto = Protocol::Auto) noexcept
        : m_address(addr)
        , m_type(type)
        , m_protocol(proto)
    {}

    [[nodiscard]] static SocketEndpoint CreateTCP(SocketAddress addr) noexcept
    {
        return SocketEndpoint{ addr, SocketType::Stream, Protocol::TCP };
    }

    [[nodiscard]] static SocketEndpoint CreateUDP(SocketAddress addr) noexcept
    {
        return SocketEndpoint{ addr, SocketType::Datagram, Protocol::UDP };
    }

    [[nodiscard]] static SocketEndpoint CreateSCTP(SocketAddress addr) noexcept
    {
        return SocketEndpoint{ addr, SocketType::SeqPacket, Protocol::SCTP };
    }

    [[nodiscard]] static SocketEndpoint CreateSeqPacket(SocketAddress addr, Protocol proto = Protocol::Auto) noexcept
    {
        return SocketEndpoint{ addr, SocketType::SeqPacket, proto };
    }

    [[nodiscard]] static SocketEndpoint CreateRdm(SocketAddress addr, Protocol proto = Protocol::Auto) noexcept
    {
        return SocketEndpoint{ addr, SocketType::Rdm, proto };
    }

    [[nodiscard]] const SocketAddress& GetAddress() const noexcept { return m_address; }
    [[nodiscard]] SocketAddress& GetAddress() noexcept { return m_address; }
    void SetAddress(SocketAddress addr) noexcept { m_address = std::move(addr); }

    [[nodiscard]] SocketType GetType() const noexcept { return m_type; }
    [[nodiscard]] int GetRawType() const noexcept { return static_cast<int>(m_type); }
    void SetType(SocketType type) noexcept { m_type = type; }

    [[nodiscard]] Protocol GetProtocol() const noexcept { return m_protocol; }
    [[nodiscard]] int GetRawProtocol() const noexcept { return static_cast<int>(m_protocol); }
    void SetProtocol(Protocol proto) noexcept { m_protocol = proto; }

    [[nodiscard]] int GetFamily() const noexcept
    {
        return m_address.getFamily();
    }

    [[nodiscard]] SocketT CreateSocket() const noexcept
    {
        return ::socket(GetFamily(), GetRawType(), GetRawProtocol());
    }

    static bool GetPeerEndpoint(SocketT sock, SocketEndpoint& outEndpoint) noexcept
    {
        return InternalGetEndpoint(sock, outEndpoint, ::getpeername);
    }

    static bool GetLocalEndpoint(SocketT sock, SocketEndpoint& outEndpoint) noexcept
    {
        return InternalGetEndpoint(sock, outEndpoint, ::getsockname);
    }

    [[nodiscard]] static std::optional<SocketEndpoint> FromPeer(SocketT sock) noexcept
    {
        SocketEndpoint ep{};
        if (GetPeerEndpoint(sock, ep)) {
            return ep;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<SocketEndpoint> FromLocal(SocketT sock) noexcept
    {
        SocketEndpoint ep{};
        if (GetLocalEndpoint(sock, ep)) {
            return ep;
        }
        return std::nullopt;
    }

private:

    template <typename Func>
    static bool InternalGetEndpoint(SocketT sock, SocketEndpoint& outEndpoint, Func&& addressFunc) noexcept
    {
        if (!IsValidSocket(sock)) {
            return false;
        }

        sockaddr_storage storage{};
        socklen_t addrLen = sizeof(storage);

        if (addressFunc(sock, reinterpret_cast<sockaddr*>(&storage), &addrLen) != 0) {
            return false;
        }

        SocketAddress remoteAddr = SocketAddress::FromRaw(reinterpret_cast<const sockaddr*>(&storage), addrLen);

        int sockType = 0;
        socklen_t optLen = sizeof(sockType);
        if (::getsockopt(sock, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&sockType), &optLen) != 0) {
            return false;
        }

        int protocol = 0;
        optLen = sizeof(protocol);
#ifdef SO_PROTOCOL
        ::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL, reinterpret_cast<char*>(&protocol), &optLen);
#endif

        outEndpoint = SocketEndpoint{
            remoteAddr,
            static_cast<SocketType>(sockType),
            static_cast<Protocol>(protocol)
        };

        return true;
    }
};
}
