module;

#include <coroutine>
#include <exception>
#include <memory>
#include <semaphore>
#include <utility>
#include <variant>

export module AsyncCoreLibrary:Task;

import :SafeCoroutineHandle;

export namespace async_core::exec
{
    struct EagerStartPolicy {
        static auto initial_suspend() noexcept { return std::suspend_never{}; }
    };

    struct LazyStartPolicy {
        static auto initial_suspend() noexcept { return std::suspend_always{}; }
    };

    template <typename T>
    struct PromiseReturnBase {
        std::optional<T> value;

        template <typename U>
            requires std::convertible_to<U, T>
        void return_value(U&& val) {
            value.emplace(std::forward<U>(val));
        }
    };

    template <typename T>
    struct PromiseReturnBase<T&> {
        T* value{nullptr};

        void return_value(T& val) noexcept {
            value = std::addressof(val);
        }
    };

    template <>
    struct PromiseReturnBase<void> {
        std::monostate value{};
        void return_void() noexcept {}
    };

    template<typename T, typename Policy = LazyStartPolicy>
    class Task
    {
    public:
        using ReturnType = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

        struct promise_type : PromiseReturnBase<T>
        {
            std::shared_ptr<CoroutineControlBlock> control_block{nullptr};
            SafeCoroutineHandle safe_handle;

            std::coroutine_handle<> continuation{};
            std::exception_ptr m_exception{nullptr};
            std::binary_semaphore* m_semaphore{nullptr};

            ~promise_type()
            {
                if (control_block) {
                    control_block->state.store(CoroutineState::Destroyed, std::memory_order_release);
                    control_block->raw_handle = nullptr;
                }
            }

            Task get_return_object()
            {
                control_block = std::make_shared<CoroutineControlBlock>(typeid(promise_type));
                control_block->state = CoroutineState::Suspended;
                control_block->raw_handle = std::coroutine_handle<promise_type>::from_promise(*this);
                safe_handle.SetControl(control_block);
                return Task{safe_handle};
            }

            auto initial_suspend() noexcept {
                return Policy::initial_suspend();
            }

            auto final_suspend() noexcept
            {
                struct FinalSuspend
                {
                    bool await_ready() const noexcept { return false; }

                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                        if (h.promise().continuation) {
                            return h.promise().continuation;
                        }

                        if (h.promise().m_semaphore)
                        {
                            h.promise().m_semaphore->release();
                        }

                        return std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };

                return FinalSuspend{};
            }

            void unhandled_exception() noexcept {
                m_exception = std::current_exception();
            }

            SafeCoroutineHandle get_safe_handle() const {
                return safe_handle;
            }
        };

    private:

        SafeCoroutineHandle safe_handle;

        void destroy_frame() noexcept {
            if (safe_handle.is_valid())
            {
                safe_handle.SetState(CoroutineState::Destroyed);
                safe_handle.raw_handle<promise_type>().destroy();
            }
        }

    public:
        explicit Task(SafeCoroutineHandle handle) : safe_handle(std::move(handle)) {}

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : safe_handle(std::exchange(other.safe_handle, SafeCoroutineHandle{})) {}

        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                destroy_frame();
                safe_handle = std::exchange(other.safe_handle, SafeCoroutineHandle{});
            }
            return *this;
        }

        ~Task() {
            destroy_frame();
        }

        SafeCoroutineHandle get_safe_handle() const {
            return safe_handle;
        }

        T get()
        {
            if (!safe_handle.is_valid()) {
                throw std::runtime_error("Attempting to run an uninitialized task.");
            }

            if (!safe_handle.raw_handle<promise_type>().done())
            {
                std::binary_semaphore sem{0};
                safe_handle.raw_handle<promise_type>().promise().m_semaphore = &sem;

                if (!safe_handle.try_resume()) {
                    safe_handle.raw_handle<promise_type>().promise().m_semaphore = nullptr;
                    throw std::runtime_error("Failed to resume task in Task::get().");
                }

                sem.acquire();
            }

            if (safe_handle.raw_handle<promise_type>().promise().m_exception)
            {
                std::rethrow_exception(safe_handle.raw_handle<promise_type>().promise().m_exception);
            }

            if constexpr (std::is_void_v<T>) {
                return;
            } else if constexpr (std::is_reference_v<T>) {
                return *safe_handle.raw_handle<promise_type>().promise().value;
            } else {
                return std::move(*safe_handle.raw_handle<promise_type>().promise().value);
            }
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return !safe_handle.is_valid() || safe_handle.raw_handle<promise_type>().done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
        {
            auto h = safe_handle.raw_handle<promise_type>();
            h.promise().continuation = caller;
            return h;
        }

        T await_resume()
        {
            if (!safe_handle.is_valid()) {
                throw std::runtime_error("co_awaiting an invalid or moved-from Task");
            }

            if (safe_handle.raw_handle<promise_type>().promise().m_exception) {
                std::rethrow_exception(safe_handle.raw_handle<promise_type>().promise().m_exception);
            }

            if constexpr (std::is_void_v<T>) {
                return;
            } else if constexpr (std::is_reference_v<T>) {
                return *safe_handle.raw_handle<promise_type>().promise().value;
            } else {
                return std::move(*safe_handle.raw_handle<promise_type>().promise().value);
            }
        }
    };

    template <typename T>
    using LazyTask = Task<T, LazyStartPolicy>;

    template <typename T>
    using EagerTask = Task<T, EagerStartPolicy>;
}
