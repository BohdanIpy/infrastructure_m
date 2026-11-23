module;

#include <stop_token>

export module AsyncCoreLibrary:TaskCancellable;

import :SafeCoroutineHandle;
import :TaskTypes;
import :Concepts;

namespace async_core::exec {
using common::HasSafeHandleConcept;
using common::IsWiredTaskEsConcept;
using common::SafeCoroutineHandle;
} // namespace async_core::exec

export namespace async_core::exec {
template <typename TaskT> struct TaskCancellable {
    void RequestStop() const noexcept {
        check();
        self().GetSafeHandle().RequestStop();
    }

    [[nodiscard]] bool IsCancelled() const noexcept {
        check();
        return self().GetSafeHandle().IsCancelled();
    }

    [[nodiscard]] std::stop_token GetStopToken() const noexcept {
        check();
        return self().GetSafeHandle().GetStopToken();
    }

  private:
    const TaskT& self() const noexcept { return static_cast<const TaskT&>(*this); }
    static constexpr void check() {
        static_assert(IsWiredTaskEsConcept<typename TaskT::task_type>,
                      "only wired tasks can be cancelled through the task object");
        static_assert(HasSafeHandleConcept<TaskT>,
                      "Task<> type must have a GetSafeHandle() function");
    }
};
} // namespace async_core::exec
