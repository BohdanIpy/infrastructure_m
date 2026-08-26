module;

#include <memory>
#include <thread>
#include <iostream>
#include <coroutine>
#include <system_error>
#include <span>

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
                    // cqe->res stores byte count for I/O operations,
                    // or poll bitmask (POLLIN/POLLOUT) for POLL operations.
                    ctx->result = cqe->res;

                    if (ctx->on_complete) {
                        ctx->on_complete(ctx, cqe->res);
                    }
                    else if (ctx->coroutine) {
                        // Pass execution back to the suspended coroutine
                        m_scheduler->schedule(ctx->coroutine);
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

        // --- Submission Helpers ---

        void submit_read(SocketT fd, std::span<std::byte> buf, IoCtx* ctx) {
            if (!ctx) return;
            ctx->type = EventType::READ;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_read(sqe, fd, buf.data(), static_cast<unsigned int>(buf.size()), 0);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_write(SocketT fd, std::span<const std::byte> buf, IoCtx* ctx) {
            if (!ctx) return;
            ctx->type = EventType::WRITE;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_write(sqe, fd, buf.data(), static_cast<unsigned int>(buf.size()), 0);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_accept(SocketT listen_fd, sockaddr* addr, socklen_t* addrlen, IoCtx* ctx) {
            if (!ctx) return;
            ctx->type = EventType::ACCEPT;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_accept(sqe, listen_fd, addr, addrlen, 0);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_close(SocketT fd, IoCtx* ctx) {
            if (!ctx) return;
            ctx->type = EventType::CLOSE;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_close(sqe, fd);
            io_uring_sqe_set_data(sqe, ctx);
            io_uring_submit(&m_ring);
        }

        void submit_poll(SocketT fd, short poll_events, IoCtx* ctx) {
            if (fd == -1 || poll_events == 0 || !ctx) return;

            ctx->type = EventType::POLL;

            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            if (!sqe) return;

            io_uring_prep_poll_add(sqe, fd, static_cast<unsigned int>(poll_events));
            io_uring_sqe_set_data(sqe, ctx);

            io_uring_submit(&m_ring);
        }
    };
}