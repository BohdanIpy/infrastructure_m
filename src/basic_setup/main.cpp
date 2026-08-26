#include <iostream>

import NetworkCoreLibrary;

using namespace network_core::core_api;
using namespace network_core::socket_address;
using namespace network_core::socket_endpoint;
using namespace network_core::basic_socket;


void setup()
{
    auto addr = SocketAddress::Any(NetSocketIpType::IP_V4, 8080);
    auto endpoint = SocketEndpoint::CreateTCP(addr);

    // 2. Configure options
    BasicSocket::Options opts;
    opts.reuse_address = true;
    opts.non_blocking = true;

    // 3. Create, Bind & Listen
    auto [server_socket, err] = BasicSocket::Create(endpoint, opts);
    if (err) {
        std::cerr << "Failed to create socket: " << err.message() << "\n";
        return;
    }

    if (auto bind_err = server_socket.Bind(); bind_err) {
        std::cerr << "Bind error: " << bind_err.message() << "\n";
        return;
    }

    if (auto listen_err = server_socket.Listen(); listen_err) {
        std::cerr << "Listen error: " << listen_err.message() << "\n";
        return;
    }

    std::cout << "Server successfully listening on "
              << server_socket.GetEndpoint().GetAddress().toString() << "\n";
}

int main(int argc, char* argv[])
{
    setup();
    return 0;
}
