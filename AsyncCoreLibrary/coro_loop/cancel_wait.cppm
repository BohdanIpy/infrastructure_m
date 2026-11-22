module;

#include <memory>

export module AsyncCoreLibrary:CancelWait;

import :WaitState;
import :ICoroScheduler;
import :Wake;

namespace async_core::coro_loop {
using async_core::common::WaitState;
using async_core::coro_loop::wake;
} // namespace async_core::coro_loop

export namespace async_core::coro_loop {
/**
 * @brief Functor, which define how cancellation_callback would look like for one co_await call. It
 * would cancell only one co_await.
 */
struct CancelWait {
    std::shared_ptr<WaitState> state;
    ICoroScheduler* scheduler;

    void operator()() noexcept {
        bool resume_needed = false;
        if (state->Complete(WaitState::Cancelled, resume_needed) && resume_needed) {
            wake(*scheduler, *state);
        }
    }
};
} // namespace async_core::coro_loop
