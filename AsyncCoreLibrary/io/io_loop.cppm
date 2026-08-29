module;

#include <memory>
#include <thread>
#include <iostream>
#include <coroutine>
#include <system_error>
#include <span>
#include <cstring>

#include <liburing.h>
#include <poll.h>

export module AsyncCoreLibrary:IoLoop;

import :ICoroScheduler;
import :IoTypes;

using async_core::coro_loop::ICoroScheduler;

export namespace async_core::io
{
    class IoQueue {
        ICoroScheduler* m_scheduler{nullptr};
        io_uring m_ring{};
        std::unique_ptr<std::jthread> m_thread;

        void working_loop(const std::stop_token& stop_token) {
            std::cout << "Started io loop\n";
            while (!stop_token.stop_requested()) {
                io_uring_cqe* cqe = nullptr;

                int ret = io_uring_wait_cqe(&m_ring, &cqe);
                [[unlikely]]
                if (ret < 0) {
                    if (-ret == EINTR) continue;
                    if (-ret == ECANCELED || stop_token.stop_requested()) break;
                    continue;
                }

                auto* ctx = static_cast<IoCtx*>(io_uring_cqe_get_data(cqe));
                if (ctx) {
                    ctx->result = cqe->res;

                    if (ctx->on_complete) {
                        ctx->on_complete(ctx, cqe->res);
                    }
                    else if (ctx->safe_coro.is_valid()) {
                        if (m_scheduler) {
                            m_scheduler->schedule(std::move(ctx->safe_coro));
                        } else {
                            ctx->safe_coro.try_resume();
                        }
                    }
                }

                io_uring_cqe_seen(&m_ring, cqe);
            }
            std::cout << "End of io loop\n";
        }

    public:
        explicit IoQueue(ICoroScheduler* scheduler, const uint32_t entries = 512)
            : m_scheduler(scheduler)
        {
            if (int ret = io_uring_queue_init(entries, &m_ring, 0); ret < 0) {
                throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
            }

            m_thread = std::make_unique<std::jthread>(
                [this](std::stop_token st) { working_loop(st); }
            );
        }

        ~IoQueue() {
            if (m_thread) {
                m_thread->request_stop();

                if (m_thread->joinable()) {
                    if (io_uring_sqe* sqe = io_uring_get_sqe(&m_ring)) {
                        io_uring_prep_nop(sqe);
                        io_uring_sqe_set_data(sqe, nullptr);
                    }
                    io_uring_submit(&m_ring);
                    m_thread->join();
                }
            }
            io_uring_queue_exit(&m_ring);
        }

        // Prevent copies/moves due to thread/ring ownership
        IoQueue(const IoQueue&) = delete;
        IoQueue& operator=(const IoQueue&) = delete;

        [[nodiscard]]
        ICoroScheduler* GetScheduler() const
        {
            return m_scheduler;
        }

        // =========================================================================
        // General File & Socket Base Operations
        // =========================================================================
        void submit_read(const SocketT fd, IoCtx* ctx) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_read(sqe, fd, ctx->payload.stream.buffer.data(), static_cast<unsigned int>(ctx->payload.stream.buffer.size()), ctx->payload.stream.offset);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_write(const SocketT fd, IoCtx* ctx) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_write(sqe, fd, ctx->payload.stream.buffer.data(), static_cast<unsigned int>(ctx->payload.stream.buffer.size()), ctx->payload.stream.offset);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_close(const SocketT fd, IoCtx* ctx) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_close(sqe, fd);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_poll(const SocketT fd, const short poll_events, IoCtx* ctx) {
            if (fd == -1 || poll_events == 0 || !ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_poll_add(sqe, fd, static_cast<unsigned int>(poll_events));
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        // =========================================================================
        // High-Level Socket Connection Operations (Accept, Connect, Shutdown)
        // =========================================================================
        void submit_accept(const SocketT listen_fd, IoCtx* ctx, const int flags = 0) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            auto* conn = &ctx->payload.connection;
            io_uring_prep_accept(sqe, listen_fd, reinterpret_cast<sockaddr*>(&conn->addr), &conn->addrlen, flags);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_connect(const SocketT fd, IoCtx* ctx) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_connect(sqe, fd, reinterpret_cast<sockaddr*>(&ctx->payload.connection.addr), ctx->payload.connection.addrlen);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_shutdown(const SocketT fd, const int how, IoCtx* ctx) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_shutdown(sqe, fd, how);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        // =========================================================================
        // TCP Stream Operations (Direct Recv / Send)
        // =========================================================================
        void submit_tcp_socket_read(SocketT fd, IoCtx* ctx, int flags = 0) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_recv(sqe, fd, ctx->payload.stream.buffer.data(), ctx->payload.stream.buffer.size(), flags);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_read_socket(SocketT fd, IoCtx* ctx, int flags = 0) {
            submit_tcp_socket_read(fd, ctx, flags);
        }

        void submit_tcp_socket_write(const SocketT fd, IoCtx* ctx, const int flags = 0) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_send(sqe, fd, ctx->payload.stream.buffer.data(), ctx->payload.stream.buffer.size(), flags);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        // =========================================================================
        // UDP / Datagram Operations (msghdr Recv / Send with Peer Addresses)
        // =========================================================================
        void submit_udp_socket_read(const SocketT fd, IoCtx* ctx, const int flags = 0) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_recvmsg(sqe, fd, &ctx->payload.datagram.msg, flags);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_read_socket_from(const SocketT fd, IoCtx* ctx, const int flags = 0) {
            submit_udp_socket_read(fd, ctx, flags);
        }

        void submit_udp_socket_write(const SocketT fd, IoCtx* ctx, const int flags = 0) {
            if (!ctx) return;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_sendmsg(sqe, fd, &ctx->payload.datagram.msg, flags);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }
    };
}