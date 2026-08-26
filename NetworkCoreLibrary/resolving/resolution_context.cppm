module;

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

export namespace network_core::resolution_context
{
    struct DnsResolveContext {
        std::vector<SocketAddress> ips;
        int status{ARES_SUCCESS};
        std::coroutine_handle<> awaiting_coroutine{nullptr};
        std::atomic<bool> is_ready{false};
    };

    inline void c_ares_addrinfo_cb(void* arg, int status, int timeouts, struct ares_addrinfo* res) {
        // Reclaim raw pointer to std::shared_ptr to manage memory lifetime safely
        std::unique_ptr<std::shared_ptr<DnsResolveContext>> ctx_ptr(
            static_cast<std::shared_ptr<DnsResolveContext>*>(arg)
        );
        auto context = *ctx_ptr;

        context->status = status;

        if (status == ARES_SUCCESS && res) {
            for (auto* node = res->nodes; node != nullptr; node = node->ai_next) {
                if (node->ai_family == AF_INET) {
                    context->ips.push_back(SocketAddress::FromRaw(node->ai_addr, sizeof(sockaddr_in)));
                } else if (node->ai_family == AF_INET6) {
                    context->ips.push_back(SocketAddress::FromRaw(node->ai_addr, sizeof(sockaddr_in6)));
                }
            }
            ::ares_freeaddrinfo(res);
        }

        if (context->is_ready.exchange(true, std::memory_order_acq_rel)) {
            if (context->awaiting_coroutine) {
                context->awaiting_coroutine.resume();
            }
        }
    }
}