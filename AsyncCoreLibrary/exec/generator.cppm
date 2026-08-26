module;

#include <exception>
#include <coroutine>
#include <iterator>
#include <utility>


export module AsyncCoreLibrary:Generator;

export namespace async_core::exec
{
    template <typename T>
    class Generator
    {
    public:
        struct promise_type
        {
            T value{};
            std::exception_ptr m_exception{nullptr};

            Generator get_return_object()
            {
                return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            auto initial_suspend() noexcept { return std::suspend_always{}; }
            auto final_suspend() noexcept { return std::suspend_always{}; }

            auto yield_value(T val)
            {
                value = std::move(val);
                return std::suspend_always{};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
                m_exception = std::current_exception();
            }
        };

        struct ForwardIterator
        {
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = const T*;
            using reference = const T&;
            using iterator_category = std::input_iterator_tag;

            std::coroutine_handle<promise_type> m_handle{nullptr};

            ForwardIterator() = default;
            explicit ForwardIterator(std::coroutine_handle<promise_type> handle) : m_handle{handle} {}

            ForwardIterator& operator++() {
                m_handle.resume();
                if (m_handle.promise().m_exception) {
                    std::rethrow_exception(m_handle.promise().m_exception);
                }
                return *this;
            }

            const T& operator*() const {
                return m_handle.promise().value;
            }

            bool operator==(std::default_sentinel_t) const {
                return !m_handle || m_handle.done();
            }
        };

        std::coroutine_handle<promise_type> m_handle{nullptr};

        explicit Generator(std::coroutine_handle<promise_type> h) : m_handle(h) {}

        ~Generator() {
            if (m_handle) {
                m_handle.destroy();
            }
        }

        Generator(const Generator&) = delete;
        Generator& operator=(const Generator&) = delete;

        Generator(Generator&& other) noexcept
        {
            this->m_handle = std::exchange(other.m_handle, nullptr);
        }

        Generator& operator=(Generator&& other) noexcept
        {
            if (this != &other)
            {
                if (this->m_handle)
                    this->m_handle.destroy();
                this->m_handle = std::exchange(other.m_handle, nullptr);
            }
            return *this;
        }

        ForwardIterator begin() {
            if (m_handle) {
                m_handle.resume();
                if (m_handle.promise().m_exception) {
                    std::rethrow_exception(m_handle.promise().m_exception);
                }
            }
            return ForwardIterator{m_handle};
        }

        std::default_sentinel_t end() {
            return {};
        }

        bool move_next()
        {
            if (!m_handle || m_handle.done())
                return false;

            m_handle.resume();

            if (m_handle.promise().m_exception) {
                std::rethrow_exception(m_handle.promise().m_exception);
            }

            return !m_handle.done();
        }

        T current_value() const
        {
            return this->m_handle.promise().value;
        }
    };
}
