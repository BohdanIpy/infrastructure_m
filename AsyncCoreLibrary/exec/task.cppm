module;

#include <coroutine>
#include <exception>
#include <semaphore>
#include <utility>
#include <memory>
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
        T value{};

        void return_value(T val) {
            value = std::move(val);
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
            std::shared_ptr<SafeCoroutineHandle> safe_handle{nullptr};

            std::coroutine_handle<> continuation{};
            std::exception_ptr m_exception{nullptr};
            std::binary_semaphore* m_semaphore{nullptr};

            ~promise_type()
            {
                if (control_block) {
                    if (control_block->state.load(std::memory_order_relaxed) == CoroutineState::Destroyed)
                        return;
                    control_block->state.store(CoroutineState::Destroyed, std::memory_order_release);
                    // if (control_block->raw_handle.done())
                    // {
                    //     control_block->raw_handle.destroy();
                    // }
                }
            }

            Task get_return_object()
            {
                control_block = std::make_shared<CoroutineControlBlock>(typeid(promise_type));
                control_block->state = CoroutineState::Suspended;
                control_block->raw_handle = std::coroutine_handle<promise_type>::from_promise(*this);
                safe_handle = std::make_shared<SafeCoroutineHandle>();
                safe_handle->SetControl(control_block);
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

            std::shared_ptr<SafeCoroutineHandle> get_safe_handle() const {
                return safe_handle;
            }
        };

    private:

        std::shared_ptr<SafeCoroutineHandle> safe_handle{nullptr};

        void destroy_frame() noexcept {
            if (safe_handle) {
                safe_handle->SetState(CoroutineState::Destroyed);
                safe_handle->raw_handle<promise_type>().destroy();
                safe_handle = nullptr;
            }
        }

    public:
        explicit Task(std::shared_ptr<SafeCoroutineHandle> handle) : safe_handle(std::move(handle)) {}

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : safe_handle(std::exchange(other.safe_handle, nullptr)) {}

        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                destroy_frame();
                safe_handle = std::exchange(other.safe_handle, nullptr);
            }
            return *this;
        }

        ~Task() {
            destroy_frame();
        }

        std::shared_ptr<SafeCoroutineHandle> get_safe_handle() const {
            return safe_handle;
        }

        T get()
        {
            if (!safe_handle) {
                throw std::runtime_error("Attempting to run an uninitialized task.");
            }

            if (!safe_handle->raw_handle<promise_type>().done())
            {
                std::binary_semaphore sem{0};
                safe_handle->raw_handle<promise_type>().promise().m_semaphore = &sem;

                safe_handle->try_resume();

                sem.acquire();
            }

            if (safe_handle->raw_handle<promise_type>().promise().m_exception)
            {
                std::rethrow_exception(safe_handle->raw_handle<promise_type>().promise().m_exception);
            }

            if constexpr (!std::is_void_v<T>)
            {
                return std::move(safe_handle->raw_handle<promise_type>().promise().value);
            }
            else
            {
                return;
            }
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
        {
            safe_handle->raw_handle<promise_type>().promise().continuation = caller;
            return safe_handle->raw_handle<promise_type>();
        }

        T await_resume()
        {
            if (safe_handle->raw_handle<promise_type>().promise().m_exception)
            {
                std::rethrow_exception(safe_handle->raw_handle<promise_type>().promise().m_exception);
            }

            if constexpr (!std::is_void_v<T>)
            {
                return std::move(safe_handle->raw_handle<promise_type>().promise().value);
            }
            else
            {
                return;
            }
        }
    };

    template <typename T>
    using LazyTask = Task<T, LazyStartPolicy>;

    template <typename T>
    using EagerTask = Task<T, EagerStartPolicy>;
}
