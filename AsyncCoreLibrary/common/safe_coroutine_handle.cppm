module;

#include <atomic>
#include <coroutine>
#include <memory>
#include <mutex>
#include <typeindex>
#include <utility>

export module AsyncCoreLibrary:SafeCoroutineHandle;

import :CoroutineStates;
import :CancellingRoutines;

using async_core::common::IPromiseStopControl;

export namespace async_core::common
{
    /**
     * Safe wrapper along the coroutine_handle<>
     */
    struct CoroutineControlBlock {
        std::atomic<CoroutineState> state{CoroutineState::Created};
        std::coroutine_handle<> raw_handle{nullptr};
        std::type_index type_idx;
        mutable std::mutex mtx;

        explicit CoroutineControlBlock(std::coroutine_handle<> handle, std::type_index idx) :
            raw_handle(std::move(handle)),
            type_idx(std::move(idx)) {}

        void set_state(CoroutineState s) {
            std::lock_guard lock(mtx);
            state.store(s, std::memory_order_release);
            if (s == CoroutineState::Destroyed) {
                raw_handle = nullptr;
            }
        }

        template<std::memory_order order = std::memory_order_acquire>
        CoroutineState load_state() const {
            return state.load(order);
        }

        bool try_execute_resume() {
            std::unique_lock lock(mtx);
            CoroutineState current = state.load(std::memory_order_relaxed);
            if (current != CoroutineState::Created && current != CoroutineState::Suspended) {
                return false;
            }

            state.store(CoroutineState::Running, std::memory_order_release);
            std::coroutine_handle<> h = raw_handle;
            lock.unlock();

            if (h && !h.done()) {
                h.resume();
            }

            lock.lock();
            if (state.load(std::memory_order_relaxed) == CoroutineState::Running) {
                state.store(CoroutineState::Suspended, std::memory_order_release);
            }
            return true;
        }

        bool try_execute_destroy_unstarted() {
            std::coroutine_handle<> raw_to_destroy{nullptr};
            {
                std::lock_guard lock(mtx);
                CoroutineState expected = CoroutineState::Created;
                if (!state.compare_exchange_strong(
                        expected,
                        CoroutineState::Destroyed,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
                {
                    return false;
                }
                raw_to_destroy = std::exchange(raw_handle, nullptr);
            }

            if (raw_to_destroy) {
                raw_to_destroy.destroy();
            }
            return true;
        }

        void execute_request_stop() {
            void* addr = nullptr;
            {
                std::lock_guard lock(mtx);

                CoroutineState current = state.load(std::memory_order_relaxed);

                if (current == CoroutineState::Destroyed ||
                    current == CoroutineState::Cancelled ||
                    !raw_handle)
                {
                    return;
                }
                state.store(CoroutineState::Cancelled, std::memory_order_release);
                addr = raw_handle.address();
            }

            if (addr) {
                auto h = std::coroutine_handle<IPromiseStopControl>::from_address(addr);
                h.promise().request_stop();
            }
        }
    };

    /**
     * Class, which stores a shared_ptr to a control block of coroutine.
     * Required to store the shared_ptr to maintain the lifetime over event loop,
     * e.g. Task is finished or cancelled, but the coroutine handle remains in loop. Object would still be valid to work with
     */
    class SafeCoroutineHandle {
        std::shared_ptr<CoroutineControlBlock> m_control{nullptr};

    public:

        SafeCoroutineHandle() = default;

        /**
         * @param handle coroutine handle
         * @param idx is a type id of the promise type, so i can return the templated version of coroutine_handle<PromiseT>. Prevents bad cast errors
         */
        explicit SafeCoroutineHandle(std::coroutine_handle<> handle, std::type_index idx)
        {
            m_control = std::make_shared<CoroutineControlBlock>(std::move(handle), std::move(idx));
        }

        SafeCoroutineHandle(const SafeCoroutineHandle&) = default;
        SafeCoroutineHandle& operator=(const SafeCoroutineHandle&) = default;

        SafeCoroutineHandle(SafeCoroutineHandle&&) noexcept = default;
        SafeCoroutineHandle& operator=(SafeCoroutineHandle&&) noexcept = default;

        ~SafeCoroutineHandle() = default;

        void SetState(CoroutineState state) const
        {
            if (m_control)
            {
                m_control->set_state(state);
            }
        }

        template<std::memory_order order = std::memory_order_acquire>
        [[nodiscard]] CoroutineState LoadState() const noexcept
        {
            if (!m_control)
            {
                return CoroutineState::Destroyed;
            }
            return m_control->load_state<order>();
        }

        void SetControl(std::shared_ptr<CoroutineControlBlock> ctrl)
        {
            m_control = std::move(ctrl);
        }

        /**
         * Check if the CoroutineHandle is valid to execute: the control block is here, and also the state of coroutine is not destroyed( finished)
         */
        [[nodiscard]] bool is_valid() const noexcept {
            return m_control &&
                   LoadState() != CoroutineState::Destroyed;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return is_valid();
        }

        /**
         * Intelligent way to resume a coroutine in a safe manner
         * mutexes and both atomics are required
         * @return true - if success resume, and false in case of a problem with running
         */
        bool try_resume() const {
            if (!m_control) return false;
            return m_control->try_execute_resume();
        }

        /**
         * Try to destroy the coroutine, which has not been executed yet
         * @return true - success destroyed the unstarted coroutine, false - coroutine was previously started once
         */
        bool try_destroy_unstarted() const {
            if (!m_control) return false;
            return m_control->try_execute_destroy_unstarted();
        }

        /**
         *
         * @tparam PromiseT the type of coroutine_handle<PromiseT> we want to get
         * @return nullptr - in case of cast error, and coroutine_handle, if success
         */
        template <typename PromiseT>
        std::coroutine_handle<PromiseT> raw_handle() const {
            if (!m_control) return nullptr;

            std::lock_guard<std::mutex> lock(m_control->mtx);

#ifndef NDEBUG
            if (typeid(PromiseT) != m_control->type_idx) {
                throw std::bad_cast();
            }
#endif
            if (!m_control->raw_handle) return nullptr;

            return std::coroutine_handle<PromiseT>::from_address(m_control->raw_handle.address());
        }

        /**
         * Request stop
         */
        void request_stop() const noexcept {
            if (!m_control) return;
            // TODO analyze the cancellation utils, and think out some solution how to structure the cancellation
            m_control->execute_request_stop();
        }

        std::shared_ptr<CoroutineControlBlock> GetControlBlock() noexcept
        {
            return m_control;
        }
    };

    /**
     * Concept which used to enforce the template parameter to have a SafeCoroutineHandle - required for safe processing in library
     */
    template <typename Promise>
    concept HasSafeHandle = requires {
        { std::declval<Promise&>().get_safe_handle() } -> std::same_as<SafeCoroutineHandle>;
    };
}
