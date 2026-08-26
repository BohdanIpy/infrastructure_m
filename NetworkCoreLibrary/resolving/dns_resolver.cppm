module;

#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
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

export namespace network_core::dns_resolution
{
    class DnsResolver {
        IoQueue& m_io_queue;
        ares_channel m_channel{};
        std::unordered_map<ares_socket_t, short> m_active_sockets;

        static void sock_state_cb(void* data, ares_socket_t socket_fd, int readable, int writable) {
            auto* self = static_cast<DnsResolver*>(data);

            if (!readable && !writable) {
                self->m_active_sockets.erase(socket_fd);
                return;
            }

            short events = 0;
            if (readable) events |= POLLIN;
            if (writable) events |= POLLOUT;

            if (auto it = self->m_active_sockets.find(socket_fd); it != self->m_active_sockets.end()) {
                if (it->second == events) {
                    return;
                }
            }

            self->m_active_sockets[socket_fd] = events;
            self->drive_socket(socket_fd, events);
        }

    public:
        explicit DnsResolver(IoQueue& queue) : m_io_queue(queue) {
            ::ares_library_init(ARES_LIB_INIT_ALL);

            struct ares_options options {};
            options.flags = ARES_FLAG_USEVC;
            options.sock_state_cb = &DnsResolver::sock_state_cb;
            options.sock_state_cb_data = this;

            int optmask = ARES_OPT_SOCK_STATE_CB | ARES_OPT_FLAGS;

            if (int res = ::ares_init_options(&m_channel, &options, optmask); res != ARES_SUCCESS) {
                ::ares_library_cleanup();
                throw std::runtime_error(::ares_strerror(res));
            }
        }

        ~DnsResolver() {
            ares_destroy(m_channel);
            ares_library_cleanup();
        }

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

            bool await_suspend(std::coroutine_handle<> h) {
                ctx->awaiting_coroutine = h;

                struct ares_addrinfo_hints hints {};
                hints.ai_family = opts.family;
                hints.ai_socktype = opts.socktype;

                auto cb_arg = new std::shared_ptr<DnsResolveContext>(ctx);

                ::ares_getaddrinfo(
                    resolver.m_channel,
                    hostname.c_str(),
                    opts.service.empty() ? nullptr : opts.service.c_str(),
                    &hints,
                    c_ares_addrinfo_cb,
                    cb_arg
                );

                ares_process_fd(resolver.m_channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);

                if (ctx->is_ready.exchange(true, std::memory_order_acq_rel)) {
                    delete cb_arg;
                    return false;
                }

                return true;
            }

            std::vector<SocketAddress> await_resume() {
                if (ctx->status != ARES_SUCCESS) {
                    throw std::runtime_error(::ares_strerror(ctx->status));
                }
                return std::move(ctx->ips);
            }
        };

        [[nodiscard]] ResolverAwaitable resolve(std::string name, ResolveOptions options) {
            return ResolverAwaitable{*this, std::move(name), std::move(options)};
        }

        [[nodiscard]] ResolverAwaitable resolve(std::string name, std::string service = "", int family = AF_UNSPEC) {
            return resolve(std::move(name), ResolveOptions{
                .service = std::move(service),
                .family = family,
                .socktype = SOCK_STREAM
            });
        }

    private:
        DetachedTask drive_socket(ares_socket_t fd, short events) {
            int32_t res_events = co_await AsyncPoll(m_io_queue, fd, events);

            m_active_sockets.erase(fd);

            if (res_events < 0 || (res_events & (POLLERR | POLLHUP))) {
                ares_process_fd(m_channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                co_return;
            }

            ares_socket_t read_fd  = (res_events & POLLIN)  ? fd : ARES_SOCKET_BAD;
            ares_socket_t write_fd = (res_events & POLLOUT) ? fd : ARES_SOCKET_BAD;

            ares_process_fd(m_channel, read_fd, write_fd);
        }
    };
}