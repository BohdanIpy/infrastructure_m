module;

#include <vector>
#include <iostream>
#include <string>
#include <string_view>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <unistd.h>
#include <variant>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>

export module NetworkCoreLibrary:SocketAddress;

import CoreUtility;
using core_utility::match;

export namespace network_core::socket_address
{
    enum class NetSocketIpType : uint8_t {
        IP_V4,
        IP_V6
    };

    class SocketAddress {
    public:
        static SocketAddress CreateSocketAddress(const NetSocketIpType type) {
            SocketAddress addr;
            if (type == NetSocketIpType::IP_V4) {
                sockaddr_in v4{};
                v4.sin_family = AF_INET;
                addr.address_ = v4;
            } else {
                sockaddr_in6 v6{};
                v6.sin6_family = AF_INET6;
                addr.address_ = v6;
            }
            return addr;
        }

        // setPort in respective sockaddr_ struct
        void setPort(uint16_t port) {
            std::visit(match{
                [port](sockaddr_in& v4)
                {
                    v4.sin_port  = htons(port);
                    std::cout << "setPort v4\n";
                },
                [port](sockaddr_in6& v6)
                {
                    v6.sin6_port = htons(port);
                    std::cout << "setPort v6\n";
                }
            }, address_);
        }

        // getPort in respective sockaddr_ struct
        [[nodiscard]] uint16_t getPort() const {
            return std::visit(match{
                [](const sockaddr_in& v4)
                {
                    return ntohs(v4.sin_port);
                },
                [](const sockaddr_in6& v6)
                {
                    return ntohs(v6.sin6_port);
                }
            }, address_);
        }

        // setAddress
        // set the X.X.X.X version of string to sockaddr_ struct
        bool setAddress(std::string_view ip) {
            return std::visit(match{
                [ip](sockaddr_in& v4) {
                    return ::inet_pton(AF_INET, ip.data(), &v4.sin_addr) == 1;
                },
                [ip](sockaddr_in6& v6) {
                    return ::inet_pton(AF_INET6, ip.data(), &v6.sin6_addr) == 1;
                }
            }, address_);
        }

        // returns AF_INET and AF_INET6
        [[nodiscard]] sa_family_t getFamily() const {
            return std::visit(match{
                [](const sockaddr_in& v4)  -> sa_family_t { return v4.sin_family; },
                [](const sockaddr_in6& v6) -> sa_family_t { return v6.sin6_family; }
            }, address_);
        }

        // returns a wrapper type - sockaddr* on sockaddr_in* and sockaddr_in6*
        [[nodiscard]] const sockaddr* getRawAddress() const {
            return std::visit(match{
                [](const sockaddr_in& v4)  { return reinterpret_cast<const sockaddr*>(&v4); },
                [](const sockaddr_in6& v6) { return reinterpret_cast<const sockaddr*>(&v6); }
            }, address_);
        }

        // returns sizeof(sockaddr_in) or sizeof(sockaddr_in6)
        [[nodiscard]] socklen_t getSize() const {
            return std::visit(match{
                [](const sockaddr_in&)  { return static_cast<socklen_t>(sizeof(sockaddr_in)); },
                [](const sockaddr_in6&) { return static_cast<socklen_t>(sizeof(sockaddr_in6)); }
            }, address_);
        }

        // returns a X.X.X.X string presentation form
        [[nodiscard]] std::string getIpString() const {
            char buf[INET6_ADDRSTRLEN] = {0};
            std::visit(match{
                [&buf](const sockaddr_in& v4) {
                    ::inet_ntop(AF_INET, &v4.sin_addr, buf, sizeof(buf));
                },
                [&buf](const sockaddr_in6& v6) {
                    ::inet_ntop(AF_INET6, &v6.sin6_addr, buf, sizeof(buf));
                }
            }, address_);
            return std::string{buf};
        }

        // returns pretty print ip:port
        [[nodiscard]] std::string toString() const {
            if (getFamily() == AF_INET6) {
                return "[" + getIpString() + "]:" + std::to_string(getPort());
            }
            return getIpString() + ":" + std::to_string(getPort());
        }

        // resolve using a dns
        static std::vector<SocketAddress> ResolveAll(std::string_view host, std::string_view service = "")
        {
            std::vector<SocketAddress> addresses;

            addrinfo hints{};
            hints.ai_family   = AF_UNSPEC; // IPv4 & IPv6
            hints.ai_socktype = 0;         // Any socket type (TCP/UDP)
            hints.ai_flags    = AI_ADDRCONFIG;

            std::string host_str{host};
            std::string service_str{service};
            const char* service_ptr = service_str.empty() ? nullptr : service_str.c_str();

            addrinfo* res = nullptr;
            if (::getaddrinfo(host_str.c_str(), service_ptr, &hints, &res) != 0 || !res) {
                return addresses; // Returns empty vector on failure
            }

            for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
                SocketAddress addr;

                if (p->ai_family == AF_INET) {
                    addr.address_ = *reinterpret_cast<sockaddr_in*>(p->ai_addr);
                    addresses.push_back(addr);
                } else if (p->ai_family == AF_INET6) {
                    addr.address_ = *reinterpret_cast<sockaddr_in6*>(p->ai_addr);
                    addresses.push_back(addr);
                }
            }

            ::freeaddrinfo(res);
            return addresses;
        }

        // accepts a raw sockaddr* to construct a socketAddress
        static SocketAddress FromRaw(const sockaddr* raw_addr, socklen_t len) {
            SocketAddress addr;
            if (raw_addr->sa_family == AF_INET && len >= sizeof(sockaddr_in)) {
                addr.address_ = *reinterpret_cast<const sockaddr_in*>(raw_addr);
            } else if (raw_addr->sa_family == AF_INET6 && len >= sizeof(sockaddr_in6)) {
                addr.address_ = *reinterpret_cast<const sockaddr_in6*>(raw_addr);
            } else {
                throw std::invalid_argument("Unsupported socket family or invalid size");
            }
            return addr;
        }

        static SocketAddress Any(NetSocketIpType type, uint16_t port = 0) {
            auto addr = CreateSocketAddress(type);
            std::visit(match{
                [port](sockaddr_in& v4)  { v4.sin_addr.s_addr = htonl(INADDR_ANY); v4.sin_port = htons(port); },
                [port](sockaddr_in6& v6) { v6.sin6_addr = in6addr_any;       v6.sin6_port = htons(port); }
            }, addr.address_);
            return addr;
        }

        static SocketAddress Loopback(NetSocketIpType type, uint16_t port = 0) {
            auto addr = CreateSocketAddress(type);
            std::visit(match{
                [port](sockaddr_in& v4)  { v4.sin_addr.s_addr = htonl(INADDR_LOOPBACK); v4.sin_port = htons(port); },
                [port](sockaddr_in6& v6) { v6.sin6_addr = in6addr_loopback;             v6.sin6_port = htons(port); }
            }, addr.address_);
            return addr;
        }

        bool operator==(const SocketAddress& other) const {
            if (getFamily() != other.getFamily()) return false;
            return std::memcmp(getRawAddress(), other.getRawAddress(), getSize()) == 0;
        }

        bool operator!=(const SocketAddress& other) const {
            return !(*this == other);
        }

        bool operator<(const SocketAddress& other) const {
            if (getFamily() != other.getFamily()) return getFamily() < other.getFamily();
            return std::memcmp(getRawAddress(), other.getRawAddress(), getSize()) < 0;
        }

        SocketAddress(const SocketAddress& other) = default;
        SocketAddress& operator=(const SocketAddress& other) = default;

        SocketAddress(SocketAddress&& other) noexcept = default;
        SocketAddress& operator=(SocketAddress&& other) noexcept = default;

    private:
        SocketAddress() = default;

        std::variant<sockaddr_in, sockaddr_in6> address_;

    };
}

using namespace network_core::socket_address;

template <>
struct std::hash<SocketAddress> {
    size_t operator()(const SocketAddress& addr) const noexcept {
        const std::string_view bytes(
            reinterpret_cast<const char*>(addr.getRawAddress()),
            addr.getSize()
        );
        return std::hash<std::string_view>{}(bytes);
    }
};
