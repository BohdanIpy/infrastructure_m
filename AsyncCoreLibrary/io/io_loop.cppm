module;

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include <liburing.h>

export module AsyncCoreLibrary:IoLoop;

import :ICoroScheduler;
import :SafeCoroutineHandle;
import :IoTypes;

namespace async_core::io {
using common::SafeCoroutineHandle;
using common::ScheduleStatus;
using coro_loop::ICoroScheduler;
} // namespace async_core::io

export namespace async_core::io {
/**
 * The state of ONE submitted operation. It lives inside the awaiter, i.e. in the coroutine frame,
 * and the kernel and the I/O thread hold a pointer to it until the completion arrives. That is why
 * a coroutine is NEVER resumed (and so never cancelled away) before its completion has arrived: the
 * kernel may still be writing into the buffers.
 */
struct IoCtx {
    // Init  : the coroutine is still inside await_suspend, a completion must not resume it
    // Armed : the coroutine is suspended, the completion must resume it
    // Done  : the completion has arrived
    enum Phase : std::uint8_t { Init, Armed, Done };

    SafeCoroutineHandle coro;
    std::int32_t result{
        0}; // cqe->res: >= 0 success, -errno otherwise. Written before phase becomes Done
    std::atomic<std::uint8_t> phase{Init};
    std::atomic<bool> cancel_requested{false};

    IoCtx() = default;
    IoCtx(const IoCtx&) = delete;
    IoCtx& operator=(const IoCtx&) = delete;

    /// Last step of await_suspend. false: the completion already arrived, continue inline.
    [[nodiscard]] bool arm() noexcept {
        std::uint8_t expected = Init;
        return phase.compare_exchange_strong(expected, Armed, std::memory_order_acq_rel,
                                             std::memory_order_acquire);
    }
};

/**
 * The io_uring reactor: one ring, one completion thread.
 *
 * Operations are submitted by the IoOperation awaiters (any thread). The completion thread turns
 * every CQE into "complete this IoCtx" and hands the coroutine to the scheduler (inline on the
 * completion thread only if the scheduler refuses it, so a completion is never lost).
 *
 * Cancel() asks the kernel to cancel one operation. The operation still produces its own completion
 * (usually -ECANCELED), and only that completion resumes the coroutine.
 *
 * Lifetime: the queue must outlive every operation. The destructor refuses new operations, asks the
 * kernel to cancel everything that is still in flight, and waits until all of it has completed (so
 * parked coroutines wake up with -ECANCELED instead of hanging, and no buffer is written after the
 * destructor returns).
 */
class IoQueue {
    // user_data values that are not pointers (an IoCtx is at least 4-byte aligned)
    static constexpr std::uint64_t kIgnoreTag = 1; // completion of a cancel request: nothing to do
    static constexpr std::uint64_t kWakeTag = 2;   // shutdown nudge for the completion thread

    ICoroScheduler& m_scheduler;
    io_uring m_ring{};
    std::mutex m_sq_mtx; // io_uring's submission queue is single-producer
    std::atomic<std::size_t> m_inflight{0};
    std::atomic<bool> m_closing{false}; // written under m_sq_mtx
    std::thread m_thread;

    /// Caller holds m_sq_mtx. Pushes queued SQEs to the kernel.
    int flush_locked() noexcept {
        int result = 0;
        for (int attempt = 0; attempt < 10000; ++attempt) {
            result = io_uring_submit(&m_ring);
            if (result >= 0) {
                return result;
            }
            if (result == -EINTR) {
                continue;
            }
            if (result == -EAGAIN ||
                result ==
                    -EBUSY) { // transient (completion queue pressure): give the kernel a moment
                std::this_thread::yield();
                continue;
            }
            break;
        }
        return result;
    }

    /// Caller holds m_sq_mtx. Never null in practice: a full submission queue is flushed first.
    io_uring_sqe* get_sqe_locked() noexcept {
        io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
        if (sqe == nullptr) {
            (void)flush_locked();
            sqe = io_uring_get_sqe(&m_ring);
        }
        return sqe;
    }

    void complete(IoCtx& ctx, const std::int32_t result) noexcept {
        (void)ctx.phase.load(std::memory_order_acquire); // pairs with the release store in
                                                         // IoOperation::await_suspend
        ctx.result = result;
        SafeCoroutineHandle handle = ctx.coro; // copied while the context is certainly alive
        // After this exchange a coroutine that had not armed yet continues inline and may destroy
        // `ctx`: do not touch it again.
        if (ctx.phase.exchange(IoCtx::Done, std::memory_order_acq_rel) == IoCtx::Armed) {
            if (m_scheduler.Schedule(handle) != ScheduleStatus::Success) {
                (void)handle.TryResume(); // the scheduler refused: never lose a completion
            }
        }
    }

    void run() noexcept {
        for (;;) {
            io_uring_cqe* cqe = nullptr;
            const int ret = io_uring_wait_cqe(&m_ring, &cqe);
            if (ret == -EINTR) {
                continue;
            }
            if (ret < 0) {
                std::terminate(); // the ring is broken: nothing sensible can be completed any more
            }

            const std::uint64_t tag = io_uring_cqe_get_data64(cqe);
            const std::int32_t result = cqe->res;
            io_uring_cqe_seen(&m_ring, cqe);

            if (tag != kIgnoreTag && tag != kWakeTag) {
                complete(*reinterpret_cast<IoCtx*>(tag), result);
                m_inflight.fetch_sub(1, std::memory_order_acq_rel);
            }

            if (m_closing.load(std::memory_order_acquire) &&
                m_inflight.load(std::memory_order_acquire) == 0) {
                return;
            }
        }
    }

  public:
    explicit IoQueue(ICoroScheduler& scheduler, const unsigned entries = 512)
        : m_scheduler(scheduler) {
        static_assert(alignof(IoCtx) >= 4, "IoCtx pointers must never collide with the tag values");
        if (const int ret = io_uring_queue_init(entries, &m_ring, 0); ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
        }
        try {
            m_thread = std::thread([this] { run(); });
        } catch (...) {
            io_uring_queue_exit(&m_ring);
            throw;
        }
    }

    IoQueue(const IoQueue&) = delete;
    IoQueue& operator=(const IoQueue&) = delete;
    IoQueue(IoQueue&&) = delete;
    IoQueue& operator=(IoQueue&&) = delete;

    ~IoQueue() {
        {
            std::lock_guard lock(m_sq_mtx);
            m_closing.store(true, std::memory_order_release); // from now on Submit() refuses

#ifdef IORING_ASYNC_CANCEL_ANY
            if (io_uring_sqe* sqe = get_sqe_locked()) {
                io_uring_prep_cancel64(sqe, 0, IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_ALL);
                io_uring_sqe_set_data64(sqe, kIgnoreTag);
            }
#endif
            if (io_uring_sqe* sqe = get_sqe_locked()) {
                io_uring_prep_nop(sqe);
                io_uring_sqe_set_data64(
                    sqe, kWakeTag); // wakes the completion thread if nothing is in flight
            }
            (void)flush_locked();
        }
        m_thread.join();
        io_uring_queue_exit(&m_ring);
    }

    [[nodiscard]] ICoroScheduler& GetScheduler() const noexcept { return m_scheduler; }

    /// Operations submitted and not yet completed.
    [[nodiscard]] std::size_t GetInflightCount() const noexcept {
        return m_inflight.load(std::memory_order_acquire);
    }

    /**
     * Fills one SQE through `prepare(io_uring_sqe*)` and submits it. Returns 0, or -errno if the
     * operation was NOT submitted (then no completion will ever arrive for `ctx`). After 0 the
     * completion WILL arrive, exactly once. `prepare` runs under the queue's lock: it must only
     * fill the SQE.
     */
    template <typename Prepare> [[nodiscard]] int Submit(IoCtx& ctx, Prepare&& prepare) noexcept {
        std::lock_guard lock(m_sq_mtx);
        if (m_closing.load(std::memory_order_relaxed)) {
            return -ESHUTDOWN;
        }
        io_uring_sqe* sqe = get_sqe_locked();
        if (sqe == nullptr) {
            return -EBUSY;
        }
        prepare(sqe);
        io_uring_sqe_set_data(sqe, &ctx);

        m_inflight.fetch_add(1, std::memory_order_acq_rel); // before the kernel can complete it
        // Practically unreachable on kernels with IORING_FEAT_NODROP: a failure here after the
        // retries inside flush_locked().
        if (const int ret = flush_locked(); ret < 0) {
            m_inflight.fetch_sub(1, std::memory_order_acq_rel);
            return ret;
        }
        return 0;
    }

    /// Asks the kernel to cancel the operation of `ctx`. Safe to call for an operation that already
    /// completed (as long as `ctx` is still alive): the kernel then simply finds nothing to cancel.
    void Cancel(IoCtx& ctx) noexcept {
        std::lock_guard lock(m_sq_mtx);
        if (io_uring_sqe* sqe = get_sqe_locked()) {
            io_uring_prep_cancel64(sqe, reinterpret_cast<std::uint64_t>(&ctx), 0);
            io_uring_sqe_set_data64(sqe, kIgnoreTag);
            (void)flush_locked();
        }
    }
};
} // namespace async_core::io