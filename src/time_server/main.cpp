#include <iostream>
#include <netdb.h>
#include <ostream>

#include <arpa/inet.h>
#include <sys/socket.h>

import NetworkCoreLibrary;
import CoreUtility;

using namespace network_core::socket_address;
using namespace network_core::core_api;
using namespace network_core::socket_endpoint;

int main(int argc, char* argv[])
{
    InitSocketApi();
    core_utility::CleanUpWrapper clean_up_wrapper{CloseSocketApi};

    std::cout << "Configuring a server..." << std::endl;
    core_utility::GetTime();
    const auto addr = SocketAddress::Any(NetSocketIpType::IP_V6, 50'000);
    const SocketEndpoint endpoint{addr, SocketType::Stream, Protocol::TCP};
    SocketT socket = endpoint.CreateSocket();

    if (!IsValidSocket(socket))
    {
        fprintf(stderr, "socket() failed. (%d)\n", GetSocketErrno());
        return 1;
    }

    const auto* raw = addr.getRawAddress();
    socklen_t len = {0};
    if (raw->sa_family == AF_INET)
        len = sizeof(sockaddr_in);
    else if (raw->sa_family == AF_INET6)
        len = sizeof(sockaddr_in6);

    if (bind(socket, raw, len)) {
        fprintf(stderr, "bind() failed. (%d)\n", GetSocketErrno());
        return 1;
    }
    std::cout << "Binding the socket successfully." << std::endl;

    if ( listen(socket, 10) < 0)
    {
        fprintf(stderr, "listen() failed. (%d)\n", GetSocketErrno());
        return 1;
    }
    std::cout << "Listening...\n";

    while (true)
    {
        sockaddr_storage client_storage;
        socklen_t addr_len = sizeof(client_storage);
        SocketT accepted_socket = accept(socket, reinterpret_cast<sockaddr*>(&client_storage), &addr_len);
        if (!IsValidSocket(accepted_socket))
        {
            fprintf(stderr, "accept() failed. (%d)\n", GetSocketErrno());
            continue;
        }
        std::cout << "Client is connected." << std::endl;

        char host_info[NI_MAXHOST];
        char port_info[NI_MAXSERV];

        int result = getnameinfo(
            (struct sockaddr*)&client_storage,
            addr_len,
            host_info, sizeof(host_info),
            port_info, sizeof(port_info),
            NI_NUMERICHOST | NI_NUMERICSERV
        );

        if (result == 0) {
            std::cout << host_info << ":" << port_info << std::endl;
        } else {
            std::cerr << "getnameinfo failed: " << gai_strerror(result) << std::endl;
        }

        std::cout << "\n===\n";
        printf("Reading request...\n");
        char request[1024];
        std::size_t bytes_received = recv(accepted_socket, request, 1024, 0);
        printf("Received %lu bytes.\n", bytes_received);
        auto data = core_utility::GetTimeString();
        std::size_t bytes_send = send(accepted_socket, data.c_str(), data.size(), 0);
        printf("Sent %lu bytes.\n", bytes_send);
        CloseSocket(accepted_socket);
    }

    CloseSocket(socket);

    // auto vec = SocketAddress::ResolveAll("google.com");
    //
    // network_core::network_adapters::ListAdapters(vec);



    return 0;
}
