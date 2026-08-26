module;

#include <coroutine>
#include <exception>
#include <semaphore>
#include <utility>

export module AsyncCoreLibrary:Task;

export namespace async_core::exec
{
    struct EagerStartPolicy {
        static auto initial_suspend() noexcept { return std::suspend_never{}; }
    };

    struct LazyStartPolicy {
        static auto initial_suspend() noexcept { return std::suspend_always{}; }
    };

    template<typename T, typename Policy = LazyStartPolicy>
    class Task
    {
    public:

        struct promise_type
        {
            T value{};
            std::coroutine_handle<> continuation{nullptr};
            std::exception_ptr m_exception{nullptr};
            std::binary_semaphore* m_semaphore{nullptr};

            Task get_return_object()
            {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            auto initial_suspend() noexcept {
                return Policy::initial_suspend();
            }

            auto final_suspend() noexcept
            {
                struct FinalSuspend
                {
                    bool await_ready() const noexcept { return false; }

                    // Symmetric transfer back to parent continuation
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                        if (h.promise().continuation) {
                            return h.promise().continuation; // Jump directly to caller
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

            void unhandled_exception() noexcept
            {
                m_exception = std::current_exception();
            }

            void return_value(T val)
            {
                value = std::move(val);
            }
        };

        std::coroutine_handle<promise_type> m_handle{nullptr};

        explicit Task(std::coroutine_handle<promise_type> handle) : m_handle{handle} {}

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}

        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (m_handle) m_handle.destroy();
                m_handle = std::exchange(other.m_handle, nullptr);
            }
            return *this;
        }

        ~Task()
        {
            if (m_handle)
            {
                m_handle.destroy();
            }
        }

        // Driving method for top-level non-coroutine callers (like main)
        T get()
        {
            if (m_handle && !m_handle.done())
            {
                m_handle.resume(); // Kick off execution!
            }

            if (m_handle.promise().m_exception)
            {
                std::rethrow_exception(m_handle.promise().m_exception);
            }
            if constexpr (!std::is_void_v<T>)
            {
                return std::move(m_handle.promise().value);
            }
        }

        // Awaitable Interface for co_await chaining:
        bool await_ready() const noexcept
        {
            return false;
        }

        // Symmetric transfer to child coroutine
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
        {
            m_handle.promise().continuation = caller; // store parent
            return m_handle; // Tail-call jump into child
        }

        T await_resume()
        {
            if (m_handle.promise().m_exception)
            {
                std::rethrow_exception(m_handle.promise().m_exception);
            }
            if constexpr (!std::is_void_v<T>)
            {
                return std::move(m_handle.promise().value);
            }
            else
            {
                return;
            }
        }
    };

    template<>
    struct Task<void>::promise_type
    {
        std::coroutine_handle<> continuation{nullptr};
        std::exception_ptr m_exception{nullptr};
        std::binary_semaphore* m_semaphore{nullptr};

        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept { return std::suspend_always{}; }

        auto final_suspend() noexcept
        {
            struct FinalSuspend
            {
                bool await_ready() const noexcept { return false; }

                // Symmetric transfer back to parent continuation
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                    if (h.promise().continuation) {
                        return h.promise().continuation; // Jump directly to caller
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

        void unhandled_exception() noexcept
        {
            m_exception = std::current_exception();
        }

        void return_void() noexcept{}
    };

    template <typename T>
    using LazyTask = Task<T>;

    template <typename T>
    using EagerTask = Task<T, EagerStartPolicy>;

}
