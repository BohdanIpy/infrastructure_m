module;

#include <iostream>
#include <ostream>
#include <vector>

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#include <ifaddrs.h>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>

#endif

export module NetworkCoreLibrary:CoreApi;

import :SocketAddress;
import CoreUtility;

using network_core::socket_address::SocketAddress;
using core_utility::wrappers::CleanUpWrapper;

export namespace network_core::core_api
{
#ifdef _WIN32
    using SocketT = ::SOCKET;

    [[nodiscard]] constexpr bool IsValidSocket(SocketT s) noexcept
    {
        return s != INVALID_SOCKET;
    }

    inline int CloseSocket(SocketT s) noexcept
    {
        return ::closesocket(s);
    }

    inline int GetSocketErrno() noexcept
    {
        return ::WSAGetLastError();
    }
#else
    using SocketT = int;
    SocketT InvalidSocket = -1;

    [[nodiscard]] constexpr bool IsValidSocket(SocketT s) noexcept
    {
        return s >= 0;
    }

    inline int CloseSocket(SocketT s) noexcept
    {
        return ::close(s);
    }

    inline int GetSocketErrno() noexcept
    {
        return errno;
    }
#endif

    enum class SocketType : int
    {
        Stream    = SOCK_STREAM,    // TCP / Byte Stream
        Datagram  = SOCK_DGRAM,     // UDP / Unreliable Datagram
        SeqPacket = SOCK_SEQPACKET, // SCTP or Local IPC / Reliable Sequential Packets
        Rdm       = SOCK_RDM,       // Reliable Datagram (Unordered)
        Raw       = SOCK_RAW        // Raw Network Layer
    };

    enum class Protocol : int
    {
        Auto = 0,
        TCP  = IPPROTO_TCP,
        UDP  = IPPROTO_UDP,
#ifdef IPPROTO_SCTP
        SCTP = IPPROTO_SCTP
#else
        SCTP = 132 // Standard IETF protocol number for SCTP
#endif
    };

    int InitSocketApi()
    {
    #ifdef _WIN32
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d);
    #endif
        return 0;
    }

    void CloseSocketApi()
    {
    #ifdef _WIN32
        WSACleanup();
    #endif
    }
}
