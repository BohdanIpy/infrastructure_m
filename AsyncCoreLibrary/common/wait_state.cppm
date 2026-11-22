module;

#include <atomic>
#include <cstdint>
#include <utility>
#include <memory>
#include <cassert>

export module AsyncCoreLibrary:WaitState;

import :SafeCoroutineHandle;

export namespace async_core::common {
class WaitState;
using WaitRef = std::shared_ptr<WaitState>;
/**
 * @brief Control block for storing states for the awaitable
 */
class WaitState {
  public:
    /**
     * @brief Enum type for storing state for awaitable
     */
    enum Phase : std::uint8_t { Init, Armed, Completed, Cancelled };

  private:
    std::atomic<std::uint8_t> m_phase{Init};
    SafeCoroutineHandle m_handle;

  public:
    explicit WaitState(SafeCoroutineHandle handle) noexcept
        : m_handle(std::move(handle)) {}

    /**
     * @return true - means the state maachine is alive and we transitioned it, false - it means the
     * machine already finished, or cancelled
     * @arg resume_needed - specifies if the coroutine is now waiting for resumption
     */
    bool Complete(Phase outcome, bool& resume_needed) noexcept {
        assert(outcome >= Completed);
        std::uint8_t current = m_phase.load(std::memory_order_acquire);
        while (current == Init || current == Armed) {
            if (m_phase.compare_exchange_weak(current, outcome, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
                resume_needed = (current == Armed);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief If m_phase is already Completed or Cancelled
     * (for instance, an io_uring CQE arrived or a stop callback fired immediately on another thread
     * before arm() ran), arm() returns false so the caller knows not to suspend the coroutine.
     */
    [[nodiscard]] bool Arm() noexcept {
        std::uint8_t expected = Init;
        return m_phase.compare_exchange_strong(expected, Armed, std::memory_order_acq_rel,
                                               std::memory_order_acquire);
    }

    [[nodiscard]] Phase GetPhase() const noexcept {
        return static_cast<Phase>(m_phase.load(std::memory_order_acquire));
    }
    [[nodiscard]] bool IsFinished() const noexcept { return GetPhase() >= Completed; }
    [[nodiscard]] const SafeCoroutineHandle& Handle() const noexcept { return m_handle; }

    [[nodiscard]] static WaitRef Make(SafeCoroutineHandle handle) {
        return std::make_shared<WaitState>(std::move(handle));
    }
};

} // namespace async_core::common