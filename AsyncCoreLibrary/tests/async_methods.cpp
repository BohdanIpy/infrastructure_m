#include "gtest/gtest.h"

#include <span>
#include <coroutine>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

import AsyncCoreLibrary;

using namespace async_core::exec;

using async_core::io::IoQueue;
using async_core::coro_loop::CoroScheduler;
using async_core::coro_loop::ICoroScheduler;
using async_core::sync::when_all_on;
using async_core::sync::sync_wait;

using async_core::io::AsyncAccept;
using async_core::io::AsyncConnect;
using async_core::io::AsyncReadSocket;
using async_core::io::AsyncWriteSocket;
using async_core::io::AsyncShutdown;

template<std::size_t IpV, std::size_t Type>
class SocketSetupFixture : public ::testing::Test
{
protected:
    std::unique_ptr<ICoroScheduler> scheduler{nullptr};
    std::unique_ptr<IoQueue> queue{nullptr};

    int m_serverSocket{-1};
    int m_clientSocket{-1};

    sockaddr_storage m_serverAddr{};
    socklen_t m_addrLen{sizeof(sockaddr_storage)};

    // Helper function to set non-blocking mode
    static void set_nonblocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "fcntl(O_NONBLOCK) failed" << std::endl;
            std::terminate();
        }
    }

    // Helper function to set socket reuse options
    static void set_reuse_addr(int fd) {
        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
            std::terminate();
        }
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    }

    void SetUp() override
    {
        scheduler = std::make_unique<CoroScheduler>(4);
        queue = std::make_unique<IoQueue>(scheduler.get());

        int ipv = static_cast<int>(IpV);
        int type = static_cast<int>(Type);
        m_serverSocket = socket(ipv, type, 0);
        if (m_serverSocket < 0) {
            std::cerr << "server socket() failed" << std::endl;
            std::terminate();
        }

        m_clientSocket = socket(ipv, type, 0);
        if (m_clientSocket < 0) {
            std::cerr << "client socket() failed" << std::endl;
            std::terminate();
        }

        // Apply socket options and non-blocking flags
        set_reuse_addr(m_serverSocket);
        set_nonblocking(m_serverSocket);
        set_nonblocking(m_clientSocket);

        // Address Setup with scoped switch cases
        switch (IpV)
        {
        case AF_INET: {
            auto* server_addr = reinterpret_cast<sockaddr_in*>(&m_serverAddr);
            server_addr->sin_family = AF_INET;
            server_addr->sin_port = htons(19080);
            inet_pton(AF_INET, "127.0.0.1", &server_addr->sin_addr);
            m_addrLen = sizeof(sockaddr_in); // Fixed: sizeof(sockaddr_in)
            break;
        }
        case AF_INET6: {
            auto* server_addr6 = reinterpret_cast<sockaddr_in6*>(&m_serverAddr);
            server_addr6->sin6_family = AF_INET6;
            server_addr6->sin6_port = htons(19080);
            inet_pton(AF_INET6, "::1", &server_addr6->sin6_addr); // Fixed: AF_INET6 and "::1"
            m_addrLen = sizeof(sockaddr_in6); // Fixed: sizeof(sockaddr_in6)
            break;
        }
        default:
            std::cerr << "unknown address family" << std::endl;
            std::terminate();
        }

        if (bind(m_serverSocket, reinterpret_cast<const sockaddr*>(&m_serverAddr), m_addrLen) < 0)
        {
            std::cerr << "bind() failed" << std::endl;
            std::terminate();
        }

        if constexpr (Type != SOCK_DGRAM)
        {
            if (listen(m_serverSocket, 4) < 0)
            {
                std::cerr << "listen() failed" << std::endl;
                std::terminate();
            }
        }
    }

    void TearDown() override
    {
        if (m_serverSocket >= 0) close(m_serverSocket);
        if (m_clientSocket >= 0) close(m_clientSocket);
    }
};

using TcpIPv4Fixture = SocketSetupFixture<AF_INET, SOCK_STREAM>;
using UdpIPv4Fixture = SocketSetupFixture<AF_INET, SOCK_DGRAM>;
using TcpIPv6Fixture = SocketSetupFixture<AF_INET6, SOCK_STREAM>;
using UdpIPv6Fixture = SocketSetupFixture<AF_INET6, SOCK_DGRAM>;

std::string byte_vector_to_string(const std::vector<std::byte>& bytes) {
    return std::string(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    );
}

Task<void> ServerPart(IoQueue& queue, int server_fd)
{
    auto [accepted_socket, addr, addrLen] = co_await AsyncAccept(&queue, server_fd);
    EXPECT_GT(accepted_socket, 0);
    std::vector<std::byte> data;
    data.resize(40);
    std::span payload = {data.begin(), data.end()};
    std::size_t readed = co_await AsyncReadSocket(&queue, accepted_socket, payload, 0);
    data.resize(readed);
    std::string str = byte_vector_to_string(data);
    std::cout << "received data: " << str << std::endl;
    co_await AsyncShutdown(&queue, accepted_socket);
    co_return;
}

Task<void> ClientPart(IoQueue& queue, int client_fd, sockaddr_storage sockaddr_storage, socklen_t sockaddr_len)
{
    co_await AsyncConnect(&queue, client_fd, reinterpret_cast<const sockaddr*>(&sockaddr_storage), sockaddr_len);
    std::vector<std::byte> data;
    for (const auto c : "Hello World!")
    {
        data.emplace_back(static_cast<std::byte>(c));
    }
    std::span payload = {data.begin(), data.end()};
    std::size_t written = co_await AsyncWriteSocket(&queue, client_fd, payload, 0);
    std::cout << "written: " << written << std::endl;
    co_await AsyncShutdown(&queue, client_fd);
    co_return;
}

Task<void> wrap(ICoroScheduler& scheduler, IoQueue& queue, int server_fd, int client_fd, sockaddr_storage sockaddr_storage, socklen_t sockaddr_len)
{
    co_await when_all_on(&scheduler, ServerPart(queue, server_fd), ClientPart(queue, client_fd, sockaddr_storage, sockaddr_len));
}

TEST_F(TcpIPv4Fixture, SendRecv)
{
    // Force the test to fail if it hangs longer than 3 seconds
    // (Requires GTest 1.13+)
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";

    // Quick manual watchdog timeout:
    // std::thread watchdog([]() {
    //     std::this_thread::sleep_for(std::chrono::seconds(3));
    //     std::cerr << "\n[TEST TIMEOUT] Test hung in io_uring event loop!" << std::endl;
    //     std::abort();
    // });
    // watchdog.detach();

    sync_wait(wrap(*scheduler, *queue, m_serverSocket, m_clientSocket, m_serverAddr, m_addrLen));
}


