module;

#include <atomic>
#include <memory>
#include <coroutine>
#include <typeindex>
#include <utility>

export module AsyncCoreLibrary:SafeCoroutineHandle;

export namespace async_core::exec
{
    enum class CoroutineState : uint8_t {
        Suspended,
        Running,
        Destroyed
    };

    struct CoroutineControlBlock {
        std::atomic<CoroutineState> state{CoroutineState::Suspended};
        std::coroutine_handle<> raw_handle{nullptr};
        std::type_index type_idx;

        explicit CoroutineControlBlock(std::type_index idx) : type_idx(idx) {}

        void resume() {
            if (raw_handle) {
                raw_handle.resume();
            }
        }
    };

    class SafeCoroutineHandle {
        std::shared_ptr<CoroutineControlBlock> m_control{nullptr};

    public:
        SafeCoroutineHandle() = default;

        explicit SafeCoroutineHandle(std::shared_ptr<CoroutineControlBlock> ctrl)
            : m_control(std::move(ctrl)) {}

        SafeCoroutineHandle(const SafeCoroutineHandle&) = default;
        SafeCoroutineHandle& operator=(const SafeCoroutineHandle&) = default;

        SafeCoroutineHandle(SafeCoroutineHandle&&) noexcept = default;
        SafeCoroutineHandle& operator=(SafeCoroutineHandle&&) noexcept = default;

        ~SafeCoroutineHandle() = default;

        void SetState(CoroutineState state)
        {
            m_control->state.store(state, std::memory_order_release);
        }

        void SetControl(std::shared_ptr<CoroutineControlBlock> ctrl)
        {
            m_control = std::move(ctrl);
        }

        [[nodiscard]] bool is_valid() const noexcept {
            return m_control &&
                   m_control->state.load(std::memory_order_acquire) != CoroutineState::Destroyed;
        }

        bool try_resume() {
            if (!m_control) return false;

            CoroutineState expected = CoroutineState::Suspended;

            if (!m_control->state.compare_exchange_strong(
                    expected,
                    CoroutineState::Running,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return false;
            }

            m_control->resume();

            expected = CoroutineState::Running;
            m_control->state.compare_exchange_strong(
                expected,
                CoroutineState::Suspended,
                std::memory_order_release
            );

            return true;
        }

        template <typename PromiseT>
        std::coroutine_handle<PromiseT> raw_handle() const {
            if (typeid(PromiseT) != m_control->type_idx)
            {
                throw std::bad_cast();
            }
            auto *mem = m_control->raw_handle.address();
            std::coroutine_handle<PromiseT> handle = std::coroutine_handle<PromiseT>::from_address(mem);
            return handle;
        }
    };

    template <typename Promise>
    concept HasSafeHandle = requires {
        { std::declval<Promise&>().get_safe_handle() } -> std::same_as<std::shared_ptr<SafeCoroutineHandle>>;
    };
}