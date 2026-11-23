module;

export module AsyncCoreLibrary:Wake;

import :WaitState;
import :ICoroScheduler;

namespace async_core::coro_loop {
using async_core::common::WaitState;
}

export namespace async_core::coro_loop {
/**
 * @brief Try schedule on the scheduler. In case of failure - resume in place
 */
void wake(ICoroScheduler& scheduler, const WaitState& state) noexcept {
    if (scheduler.Schedule(state.Handle()) != ScheduleStatus::Success) {
        (void)state.Handle().TryResume();
    }
}

void resume_waiter(ICoroScheduler* scheduler, const WaitState& state) noexcept {
    if (scheduler != nullptr) {
        wake(*scheduler, state);
    } else {
        (void)state.Handle().TryResume();
    }
}
} // namespace async_core::coro_loop
