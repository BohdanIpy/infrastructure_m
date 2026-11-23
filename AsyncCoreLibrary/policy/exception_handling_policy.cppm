module;

#include <format>
#include <exception>

export module AsyncCoreLibrary:ExceptionHandlingPolicy;

import :TaskTypes;
import :Exceptions;
import CoreUtility;

namespace async_core::policy {
using common::OperationCancelledException;
using common::TaskType;
using core_utility::stderr_logger::GetLogger;
using core_utility::stderr_logger::StderrLogger;
} // namespace async_core::policy

export namespace async_core::policy {
// ==========================================
// Exception Handling Policies
// ==========================================

template <TaskType Type> struct ExceptionPolicy;

/** @brief Wired Task: stores the exception to be rethrown by the awaiter / Get() (see
 * BasicPromiseType::take_result)
 */
template <> struct ExceptionPolicy<TaskType::Wired> {
    std::exception_ptr m_exception{nullptr};

    void unhandled_exception() noexcept { m_exception = std::current_exception(); }

    void rethrow_if_exception() const {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
    }
};

// Detached Task: nobody awaits it, so the exception goes to the installed handler
template <> struct ExceptionPolicy<TaskType::Detached> {
    void unhandled_exception() noexcept {
        try {
            throw;
        } catch (const OperationCancelledException&) {
        } catch (const std::exception& e) {
            GetLogger().LogException("DetachedExceptionHandler", std::current_exception());
        } catch (...) {
            GetLogger().LogCritical("DetachedExceptionHandler",
                                    "[DetachedTask] unhandled exception: unknown type]");
        }
    }
};
} // namespace async_core::policy
