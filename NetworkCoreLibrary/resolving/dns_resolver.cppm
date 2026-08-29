module;

#include <iostream>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <atomic>
#include <chrono>
#include <poll.h>
#include <arpa/inet.h>

#include "ares.h"

export module NetworkCoreLibrary:DnsResolution;

import AsyncCoreLibrary;
import :ResolutionContext;
import :SocketAddress;

using namespace async_core::io;
using namespace network_core::resolution_context;
using async_core::exec::Task;
using async_core::exec::DetachedTask;
using async_core::exec::HasSafeHandle;
using async_core::coro_loop::SwitchTo;
using async_core::time::SleepFor;
using async_core::time::TimerQueue;

export namespace network_core::dns_resolution
{
    class DnsResolver;

    struct SocketStateContext {
        IoQueue* io_queue{nullptr};
        ares_channel channel{nullptr};
        DnsResolver* resolver{nullptr};
    };

    class DnsResolver {
        TimerQueue& m_timer_queue;
        IoQueue& m_io_queue;
        ares_channel m_channel{nullptr};
        SocketStateContext m_state_ctx;

        std::mutex m_sockets_mutex;
        std::unordered_map<ares_socket_t, std::shared_ptr<std::atomic<bool>>> m_active_sockets;
        std::atomic<bool> m_is_running{true};

        // creating an event for reading/writing on a socket
        static void sock_state_cb(void* data, ares_socket_t socket_fd, int readable, int writable) {
            auto* ctx = static_cast<SocketStateContext*>(data);
            if (!ctx || !ctx->resolver) return;

            auto* resolver = ctx->resolver;
            std::lock_guard lock(resolver->m_sockets_mutex);

            if (readable == 0 && writable == 0) {
                if (auto it = resolver->m_active_sockets.find(socket_fd); it != resolver->m_active_sockets.end()) {
                    it->second->store(false, std::memory_order_release);
                    resolver->m_active_sockets.erase(it);
                }
            } else {
                if (!resolver->m_active_sockets.contains(socket_fd)) {
                    auto active_flag = std::make_shared<std::atomic<bool>>(true);
                    resolver->m_active_sockets[socket_fd] = active_flag;

                    drive_socket(*ctx->io_queue, ctx->channel, socket_fd, active_flag);
                }
            }
        }

        // Detached Task for posting into event loop
        static DetachedTask drive_socket(IoQueue& io_queue, ares_channel channel,
                                         const ares_socket_t fd, std::shared_ptr<std::atomic<bool>> socket_active) {
            co_await SwitchTo{io_queue.GetScheduler(), 0};

            while (socket_active->load(std::memory_order_acquire)) {
                PollResult
                /*int32_t*/ res_events = co_await AsyncPoll(&io_queue, fd, POLLIN | POLLOUT);

                if (!socket_active->load(std::memory_order_acquire)) {
                    co_return;
                }

                if (res_events.has_error() || res_events.has_hungup() ) {
                    ares_process_fd(channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                    co_return;
                }

                const ares_socket_t read_fd  = res_events.can_read()  ? fd : ARES_SOCKET_BAD;
                const ares_socket_t write_fd = res_events.can_write() ? fd : ARES_SOCKET_BAD;

                ares_process_fd(channel, read_fd, write_fd);
            }
        }

        DetachedTask drive_timeouts() const {
            co_await SwitchTo{m_io_queue.GetScheduler(), 0};

            while (m_is_running.load(std::memory_order_relaxed)) {
                timeval tv_buf{};
                const timeval* tv = ares_timeout(m_channel, nullptr, &tv_buf);

                uint32_t timeout_ms = 1000;
                if (tv) {
                    timeout_ms = static_cast<uint32_t>((tv->tv_sec * 1000) + (tv->tv_usec / 1000));
                    if (timeout_ms == 0) timeout_ms = 50;
                }

                co_await SleepFor(m_timer_queue, std::chrono::milliseconds(timeout_ms));

                if (!m_is_running.load(std::memory_order_relaxed)) {
                    co_return;
                }

                ares_process_fd(m_channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
            }
        }

    public:
        explicit DnsResolver(IoQueue& io_queue, TimerQueue& timer_queue)
            : m_timer_queue(timer_queue),
              m_io_queue(io_queue)
        {
            if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
                std::cerr << "Failed to initialize c-ares library\n";
                std::terminate();
            }

            m_state_ctx.io_queue = &m_io_queue;
            m_state_ctx.resolver = this;

            ares_options options {};
            options.flags = ARES_FLAG_USEVC;
            options.sock_state_cb = &DnsResolver::sock_state_cb;
            options.sock_state_cb_data = &m_state_ctx;
            options.timeout = 5000;

            constexpr int optmask = ARES_OPT_SOCK_STATE_CB | ARES_OPT_FLAGS | ARES_OPT_TIMEOUTMS;

            if (const int res = ::ares_init_options(&m_channel, &options, optmask); res != ARES_SUCCESS) {
                ::ares_library_cleanup();
                throw std::runtime_error(::ares_strerror(res));
            }

            m_state_ctx.channel = m_channel;

            (void)drive_timeouts();
        }

        ~DnsResolver() {
            m_is_running.store(false, std::memory_order_release);

            if (m_channel) {
                ares_cancel(m_channel);
                {
                    std::lock_guard lock(m_sockets_mutex);
                    for (auto& [fd, active_flag] : m_active_sockets) {
                        active_flag->store(false, std::memory_order_release);
                    }
                    m_active_sockets.clear();
                }

                ares_destroy(m_channel);
                m_channel = nullptr;
            }

            ares_library_cleanup();
        }

        DnsResolver(const DnsResolver&) = delete;
        DnsResolver& operator=(const DnsResolver&) = delete;
        DnsResolver(DnsResolver&&) = delete;
        DnsResolver& operator=(DnsResolver&&) = delete;

        struct ResolveOptions {
            std::string service{""};
            int family{AF_UNSPEC};
            int socktype{SOCK_STREAM};
        };

        struct ResolverAwaitable {
            DnsResolver& resolver;
            std::string hostname;
            ResolveOptions opts;

            std::shared_ptr<DnsResolveContext> ctx = std::make_shared<DnsResolveContext>();

            bool await_ready() const noexcept { return false; }

            template <typename PromiseT>
                requires HasSafeHandle<PromiseT>
            bool await_suspend(std::coroutine_handle<PromiseT> h) {
                ctx->safeHandle = h.promise().get_safe_handle();
                ctx->scheduler = resolver.m_io_queue.GetScheduler();

                ares_addrinfo_hints hints {};
                hints.ai_family = opts.family;
                hints.ai_socktype = opts.socktype;

                ::ares_getaddrinfo(
                    resolver.m_channel,
                    hostname.c_str(),
                    opts.service.empty() ? nullptr : opts.service.c_str(),
                    &hints,
                    c_ares_addrinfo_cb,
                    ctx.get()
                );

                return !ctx->is_ready.exchange(true, std::memory_order_acq_rel);
            }

            std::vector<SocketAddress> await_resume() const
            {
                if (ctx->status == ARES_ECANCELLED) {
                    throw std::runtime_error("DNS resolution cancelled");
                }
                if (ctx->status != ARES_SUCCESS) {
                    throw std::runtime_error(::ares_strerror(ctx->status));
                }
                return std::move(ctx->ips);
            }
        };

        [[nodiscard]] ResolverAwaitable Resolve(std::string name, ResolveOptions options) {
            return ResolverAwaitable{.resolver = *this, .hostname = std::move(name), .opts = std::move(options)};
        }

        [[nodiscard]] ResolverAwaitable Resolve(std::string name, std::string service = "", const int family = AF_UNSPEC) {
            return Resolve(std::move(name), ResolveOptions{
                .service = std::move(service),
                .family = family,
                .socktype = SOCK_STREAM
            });
        }
    };
}