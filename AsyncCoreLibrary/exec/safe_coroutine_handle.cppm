module;

#include <atomic>
#include <memory>
#include <coroutine>

export module AsyncCoreLibrary:SafeCoroutineHandle;

export namespace async_core::exec
{
    struct CoroutineControlBlock {
        std::atomic<bool> is_alive{true};
        std::atomic<bool> is_done{false};
    };

    class SafeCoroutineHandle {
        std::coroutine_handle<> m_handle{nullptr};
        std::shared_ptr<CoroutineControlBlock> m_control{nullptr};

    public:
        SafeCoroutineHandle() = default;

        template <typename Promise>
        explicit SafeCoroutineHandle(std::coroutine_handle<Promise> h, std::shared_ptr<CoroutineControlBlock> ctrl)
            : m_handle(h), m_control(std::move(ctrl)) {}

        [[nodiscard]] bool is_valid() const noexcept {
            return m_control && m_control->is_alive.load(std::memory_order_acquire);
        }

        bool try_resume() {
            if (!is_valid()) return false;

            if (m_handle && !m_handle.done()) {
                m_handle.resume();
                return true;
            }
            return false;
        }

        void notify_destroyed() noexcept {
            if (m_control) {
                m_control->is_alive.store(false, std::memory_order_release);
            }
        }
    };
}