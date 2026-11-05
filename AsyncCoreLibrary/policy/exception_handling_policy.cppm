module;

#include <exception>

export module AsyncCoreLibrary:ExceptionHandlingPolicy;

import :TaskTypes;

using async_core::common::TaskType;

export namespace async_core::policy
{
    // ==========================================
    // 1. Exception Handling Policies
    // ==========================================

    template <TaskType Type>
    struct ExceptionPolicy;

    template <>
    struct ExceptionPolicy<TaskType::Wired>
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

    template <>
    struct ExceptionPolicy<TaskType::Detached>
    {
        template <typename Stream>
        void handle_exception(Stream& os) noexcept {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                os << "[DetachedTask Exception]: " << e.what() << '\n';
            } catch (...) {
                os << "[DetachedTask Exception]: Unknown exception\n";
            }
        }
    };
}
