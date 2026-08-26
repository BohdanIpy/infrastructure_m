#include <functional>
#include <coroutine>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>
#include <format>
#include <memory>
#include <condition_variable>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "io_l.hpp"
#include "coro_loop.hpp"
#include "concept.hpp"
#include "sync/sync_wait.hpp"
#include "error_l.hpp"


using namespace io;
using namespace exec;

std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(4);

DetachedTask handle_client(IoQueue& io_queue, int client_fd) {
    constexpr std::size_t BUFFER_SIZE = 1024;
    std::vector<std::byte> buffer(BUFFER_SIZE);

    try {
        while (true) {
            // 1. Read incoming data from client
            std::size_t bytes_read = co_await AsyncRead(io_queue, client_fd, buffer);
            std::cout << "Bytes read: " << bytes_read << "\n";
            // Client disconnected cleanly (EOF)
            if (bytes_read == 0) {
                break;
            }

            // 2. Echo the exact data back to the client
            std::span<const std::byte> echo_data{buffer.data(), bytes_read};
            co_await AsyncWrite(io_queue, client_fd, echo_data);
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "[Client FD " << client_fd << "] Error: " << ex.what() << "\n";
    }

    // 3. Cleanly close socket on completion or error
    co_await AsyncClose(io_queue, client_fd);
    std::cout << "[Client FD " << client_fd << "] Closed connection.\n";
}

// --- Main Accept Loop Coroutine ---
Task<void> run_echo_server(IoQueue& io_queue, int listen_fd)
{
    std::cout << "Echo Server running and waiting for connections...\n";

    while (true) {
        auto [client_fd, addr_storage, len] = co_await AsyncAccept(io_queue, listen_fd);

        char ip_str[INET6_ADDRSTRLEN] = {0};
        uint16_t port = 0;

        if (addr_storage.ss_family == AF_INET) {
            auto* addr4 = reinterpret_cast<const sockaddr_in*>(&addr_storage);
            ::inet_ntop(AF_INET, &(addr4->sin_addr), ip_str, sizeof(ip_str));
            port = ::ntohs(addr4->sin_port);
        } else if (addr_storage.ss_family == AF_INET6) {
            auto* addr6 = reinterpret_cast<const sockaddr_in6*>(&addr_storage);
            ::inet_ntop(AF_INET6, &(addr6->sin6_addr), ip_str, sizeof(ip_str));
            port = ::ntohs(addr6->sin6_port);
        }

        std::cout << "[Server] Accepted connection from "
                  << ip_str << ":" << port
                  << " (FD: " << client_fd << ")\n";

        handle_client(io_queue, client_fd);
    }
}

SocketT create_listen_socket(uint16_t port) {
    SocketT fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind failed");
        return -1;
    }

    if (::listen(fd, SOMAXCONN) < 0) {
        perror("listen failed");
        return -1;
    }

    return fd;
}

int main()
{
    IoQueue queue{scheduler.get()};
    sync_wait(run_echo_server(queue, create_listen_socket(8080)));
    return 0;
}
