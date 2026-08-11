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

export module NetworkCoreLibrary:NetworkAdapters;

import :SocketAddress;
using namespace network_core::socket_address;

export namespace network_core::network_adapters
{
    std::vector<SocketAddress> GetAllAdapters()
    {
#ifdef _WIN32

#else
        std::vector<SocketAddress> addresses;
        struct ifaddrs *ifaddresses;
        if (getifaddrs(&ifaddresses) == -1)
        {
            return {};
        }
        struct ifaddrs *address = ifaddresses;
        while (address)
        {
            int family = address->ifa_addr->sa_family;
            if (family == AF_INET || family == AF_INET6) {
                auto socketAddress = SocketAddress::FromRaw(address->ifa_addr, family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
                addresses.push_back(socketAddress);
            }
            address = address->ifa_next;
        }
        return addresses;
#endif
    }

    void ListAdapters(const std::vector<SocketAddress>& addresses)
    {
        for (auto address : addresses)
        {
            std::cout << address.toString() << std::endl;
        }
    }

    void ListAdapters()
    {
    #ifdef _WIN32
        DWORD asize = 20000;
        PIP_ADAPTER_ADDRESSES adapters;
        do {
            adapters = (PIP_ADAPTER_ADDRESSES)malloc(asize);
            if (!adapters) {
                printf("Couldn't allocate %ld bytes for adapters.\n", asize);
                WSACleanup();
                return -1;
            }
            int r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, 0,
            adapters, &asize);
            if (r == ERROR_BUFFER_OVERFLOW) {
                printf("GetAdaptersAddresses wants %ld bytes.\n", asize);
                free(adapters);
            } else if (r == ERROR_SUCCESS) {
                break;
            } else {
                printf("Error from GetAdaptersAddresses: %d\n", r);
                free(adapters);
                WSACleanup();
                return -1;
            }
        } while (!adapters);
        PIP_ADAPTER_ADDRESSES adapter = adapters;
        while (adapter) {
            printf("\nAdapter name: %S\n", adapter->FriendlyName);
            PIP_ADAPTER_UNICAST_ADDRESS address = adapter->FirstUnicastAddress;
            while (address) {
                printf("\t%s",
                address->Address.lpSockaddr->sa_family == AF_INET ?
                "IPv4" : "IPv6");
                char ap[100];
                getnameinfo(address->Address.lpSockaddr,
                address->Address.iSockaddrLength,
                ap, sizeof(ap), 0, 0, NI_NUMERICHOST);
                printf("\t%s\n", ap);
                address = address->Next;
            }
            adapter = adapter->Next;
        }
        free(adapters);
    #endif
        struct ifaddrs *ifaddresses;
        if (getifaddrs(&ifaddresses) == -1)
        {
            return;
        }
        struct ifaddrs *address = ifaddresses;
        while (address)
        {
            int family = address->ifa_addr->sa_family;
            if (family == AF_INET || family == AF_INET6) {
                printf("%s\t", address->ifa_name);
                printf("%s\t", family == AF_INET ? "IPv4" : "IPv6");
                char ap[100];
                const int family_size = family == AF_INET ?
                sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
                getnameinfo(address->ifa_addr,family_size, ap, sizeof(ap), 0, 0, NI_NUMERICHOST);
                printf("\t%s\n", ap);
            }
            address = address->ifa_next;
        }
        return;
    }
}


