#include "gtest/gtest.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

import NetworkCoreLibrary;

using namespace network_core::socket_address;

TEST(SocketAddress, defaultIPV4)
{
    auto addr = SocketAddress::CreateSocketAddress(NetSocketIpType::IP_V4);
    auto rawSockaddr = addr.getRawAddress();
    ASSERT_EQ(rawSockaddr->sa_family, AF_INET);
    ASSERT_NE(rawSockaddr->sa_family, AF_INET6);

    const auto* sa_v4 = reinterpret_cast<const sockaddr_in*>(rawSockaddr->sa_data);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sa_v4->sin_addr), ip_str, sizeof(ip_str));

    std::cout << "Detected IPv4 address: " << ip_str
              << " Port: " << ntohs(sa_v4->sin_port) << "\n";
}

TEST(SocketAddress, defaultIPV6)
{
    auto addr = SocketAddress::CreateSocketAddress(NetSocketIpType::IP_V6);
    auto rawSockaddr = addr.getRawAddress();
    ASSERT_NE(rawSockaddr->sa_family, AF_INET);
    ASSERT_EQ(rawSockaddr->sa_family, AF_INET6);

    const auto* sa_v6 = reinterpret_cast<const sockaddr_in6*>(rawSockaddr->sa_data);
    char ip_str[INET6_ADDRSTRLEN];

    inet_ntop(AF_INET6, &(sa_v6->sin6_addr), ip_str, sizeof(ip_str));

    std::cout << "Detected IPv6 address: " << ip_str
              << " Port: " << ntohs(sa_v6->sin6_port) << "\n";
}


TEST(SocketAddress, addressAndPortIPV4)
{
    auto addr = SocketAddress::CreateSocketAddress(NetSocketIpType::IP_V4);
    ASSERT_TRUE(addr.setAddress("1.2.3.4"));
    addr.setPort(8080);

    ASSERT_EQ(addr.getFamily(), AF_INET);
    ASSERT_EQ(addr.getPort(), 8080);
    ASSERT_EQ(addr.getSize(), sizeof(sockaddr_in));
    ASSERT_EQ(addr.getIpString(), "1.2.3.4");

    const auto* raw = addr.getRawAddress();
    ASSERT_NE(raw->sa_family, AF_INET6);
    const auto* sa_v4 = reinterpret_cast<const sockaddr_in*>(raw);
    char ip_str[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &sa_v4->sin_addr, ip_str, sizeof(ip_str));

    std::cout << "Detected IPv4 address: " << ip_str
    << " Port: " << ntohs(sa_v4->sin_port) << "\n";

    ASSERT_STREQ(addr.getIpString().c_str(), ip_str);
}

TEST(SocketAddress, addressAndPortIPV6)
{
    auto addr = SocketAddress::CreateSocketAddress(NetSocketIpType::IP_V6);
    ASSERT_TRUE(addr.setAddress("2001:4860:4860::8888"));
    addr.setPort(8080);

    ASSERT_EQ(addr.getFamily(), AF_INET6);
    ASSERT_EQ(addr.getPort(), 8080);
    ASSERT_EQ(addr.getSize(), sizeof(sockaddr_in6));
    ASSERT_EQ(addr.getIpString(), "2001:4860:4860::8888");

    const auto* raw = addr.getRawAddress();
    ASSERT_NE(raw->sa_family, AF_INET);
    const auto* sa_v6 = reinterpret_cast<const sockaddr_in6*>(raw);
    char ip_str[INET6_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET6, &sa_v6->sin6_addr, ip_str, sizeof(ip_str));

    std::cout << "Detected IPv6 address: " << ip_str
    << " Port: " << ntohs(sa_v6->sin6_port) << "\n";

    ASSERT_STREQ(addr.getIpString().c_str(), ip_str);
}

TEST(SocketAddress, loopbackIPV4)
{
    auto addr = SocketAddress::Loopback(NetSocketIpType::IP_V4, 80);
    ASSERT_EQ(addr.getIpString(), std::string{"127.0.0.1"});
    ASSERT_EQ(addr.getPort(), 80);
}

TEST(SocketAddress, loopbackIPV6)
{
    auto addr = SocketAddress::Loopback(NetSocketIpType::IP_V6, 80);
    ASSERT_EQ(addr.getIpString(), std::string{"::1"});
    ASSERT_EQ(addr.getPort(), 80);
}

