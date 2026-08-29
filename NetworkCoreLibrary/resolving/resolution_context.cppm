module;

#include <iostream>
#include <vector>
#include <coroutine>
#include <atomic>
#include <memory>
#include <netdb.h>
#include <arpa/inet.h>
#include "ares.h"

export module NetworkCoreLibrary:ResolutionContext;

import AsyncCoreLibrary;
import :SocketAddress;

using async_core::coro_loop::ICoroScheduler;
using async_core::exec::SafeCoroutineHandle;

export namespace network_core::resolution_context
{
    // result context after resolving
    struct DnsResolveContext : std::enable_shared_from_this<DnsResolveContext> {
        SafeCoroutineHandle safeHandle;
        ICoroScheduler* scheduler{nullptr};
        std::atomic<bool> is_ready{false};
        int status{ARES_SUCCESS};
        std::vector<SocketAddress> ips;
    };

    inline void c_ares_addrinfo_cb(void* arg, int status, int timeouts, struct ares_addrinfo* res) {
        if (!arg) return;

        auto context = static_cast<DnsResolveContext*>(arg)->shared_from_this();
        context->status = status;

        if (status == ARES_ECANCELLED) {
            if (res) {
                ::ares_freeaddrinfo(res);
            }
            if (context->is_ready.exchange(true, std::memory_order_acq_rel)) {
                if (context->safeHandle.is_valid() && context->scheduler) {
                    context->scheduler->schedule(context->safeHandle);
                }
            }
            return;
        }
        if (status == ARES_SUCCESS && res) {
            for (auto* node = res->nodes; node != nullptr; node = node->ai_next) {
                if (node->ai_family == AF_INET) {
                    context->ips.push_back(SocketAddress::FromRaw(node->ai_addr, sizeof(sockaddr_in)));
                } else if (node->ai_family == AF_INET6) {
                    context->ips.push_back(SocketAddress::FromRaw(node->ai_addr, sizeof(sockaddr_in6)));
                }
            }
        }

        if (res) {
            ares_freeaddrinfo(res);
        }
        if (context->is_ready.exchange(true, std::memory_order_acq_rel)) {
            if (context->safeHandle.is_valid() && context->scheduler) {
                context->scheduler->schedule(context->safeHandle);
            }
        }
    }
}
