module;

#include <concepts>
#include <coroutine>
#include <exception>
#include <iterator>
#include <cstddef>
#include <utility>
#include <cassert>

export module AsyncCoreLibrary:Generator;

export namespace async_core::exec {
/**
 * @brief Generator coroutine-based type
 * @tparam T type which would be generated
 */
template <typename T> class Generator {
  public:
    struct promise_type {
        bool m_started{false};
        const T* value{nullptr};
        std::exception_ptr m_exception{nullptr};

        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }

        auto yield_value(const T& val) noexcept {
            value = &val;
            return std::suspend_always{};
        }

        auto yield_value(T&& val) noexcept {
            value = &val;
            return std::suspend_always{};
        } 

        void return_void() noexcept {}

        void unhandled_exception() noexcept { m_exception = std::current_exception(); }

        template <typename U> void await_transform(U&&) = delete;
    };

  private:
    std::coroutine_handle<promise_type> m_handle{nullptr};

    void resume_and_check() {
        m_handle.resume();
        if (auto& e = m_handle.promise().m_exception) {
            std::rethrow_exception(std::exchange(e, nullptr));
        }
    }

    explicit Generator(std::coroutine_handle<promise_type> h)
        : m_handle(h) {}

  public:
    struct Iterator {
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = const T*;
        using reference = const T&;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept = std::input_iterator_tag;

        std::coroutine_handle<promise_type> m_handle{nullptr};

        Iterator() = default;
        explicit Iterator(std::coroutine_handle<promise_type> handle)
            : m_handle{handle} {}

        Iterator& operator++() {
            assert(!m_handle.done());
            m_handle.resume();
            if (m_handle.promise().m_exception) {
                auto exp = m_handle.promise().m_exception;
                m_handle.promise().m_exception = nullptr;
                std::rethrow_exception(exp);
            }
            return *this;
        }

        Iterator operator++(int) {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        const T& operator*() const {
            assert(m_handle && !m_handle.done());
            return *m_handle.promise().value;
        }

        bool operator==(std::default_sentinel_t) const { return !m_handle || m_handle.done(); }
    };

    ~Generator() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    Generator(Generator&& other) noexcept {
        this->m_handle = std::exchange(other.m_handle, nullptr);
    }

    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (this->m_handle)
                this->m_handle.destroy();
            this->m_handle = std::exchange(other.m_handle, nullptr);
        }
        return *this;
    }

    Iterator begin() {
        if (m_handle && !m_handle.done() && !std::exchange(m_handle.promise().m_started, true)) {
            resume_and_check();
        }
        return Iterator{m_handle};
    }

    bool move_next() {
        if (!m_handle || m_handle.done())
            return false;
        m_handle.promise().m_started = true;
        resume_and_check();
        return !m_handle.done();
    }

    std::default_sentinel_t end() { return {}; }

    [[nodiscard]] const T& current_value() const noexcept {
        assert(m_handle && !m_handle.done());
        return *m_handle.promise().value;
    }
};
} // namespace async_core::exec
