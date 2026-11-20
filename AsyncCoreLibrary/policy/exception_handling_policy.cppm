module;

#include <exception>
#include <iostream>

export module AsyncCoreLibrary:ExceptionHandlingPolicy;

import :TaskTypes;

using async_core::common::TaskType;

export namespace async_core::policy
{
    // ==========================================
    // Exception Handling Policies
    // ==========================================

    template <TaskType Type, typename Stream = std::ostream&>
    struct ExceptionPolicy;

    // Wired Task: Stores exception_ptr to be rethrown later by the parent/awaiter
    template <typename Stream>
    struct ExceptionPolicy<TaskType::Wired, Stream>
    {
        std::exception_ptr m_exception{nullptr};

        void unhandled_exception() noexcept {
            m_exception = std::current_exception();
        }

        void rethrow_if_exception() const {
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
        }
    };

    // Detached Task: Catches and logs the exception immediately using a provided stream
    template <typename Stream>
    struct ExceptionPolicy<TaskType::Detached, Stream>
    {
        void unhandled_exception() noexcept {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                std::cerr << "[DetachedTask Exception]: " << e.what() << '\n';
            } catch (...) {
                std::cerr << "[DetachedTask Exception]: Unknown exception\n";
            }
        }
    };
}
