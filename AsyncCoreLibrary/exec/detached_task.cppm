module;

#include <memory>
#include <utility>

export module AsyncCoreLibrary:DetachedTask;

import :SafeCoroutineHandle;
import :BasicPromiseType;
import :StartEndPolicy;
import :TaskBase;
import :TaskTypes;

namespace async_core::exec {
using common::DetachedTaskES;
using common::SafeCoroutineHandle;
using policy::AutoDestroyEndPolicy;
using policy::EagerStartPolicy;
using policy::LazyStartPolicy;
} // namespace async_core::exec

export namespace async_core::exec {
/**
 * @brief Fire-and-forget coroutine: it destroys its own frame when it finishes. The object returned
 * to the caller is only a handle; it owns nothing.
 *
 *   DetachedTask      starts at once, on the calling thread, until its first real suspension
 *   LazyDetachedTask  is created suspended and starts when somebody schedules or resumes its handle
 *                     (use it when the work must start on a scheduler worker, e.g. the runners of
 * when_all)
 *
 * Never schedule or resume a DetachedTask (eager): it is already running.
 *
 * No user-declared special members on purpose (rule of zero): a user-declared destructor would
 * suppress the implicit move constructor, and since TaskBase is non-copyable the type could then
 * not be moved at all.
 */
template <typename StartPolicy> class BasicDetachedTask : public TaskBase<DetachedTaskES> {
  public:
    using promise_type = BasicPromiseType<BasicDetachedTask, void, TaskType::Detached, StartPolicy,
                                          AutoDestroyEndPolicy>;

    using Base = TaskBase;

    explicit BasicDetachedTask(SafeCoroutineHandle handle)
        : Base(std::move(handle)) {}
};

using DetachedTask = BasicDetachedTask<EagerStartPolicy>;
using LazyDetachedTask = BasicDetachedTask<LazyStartPolicy>;
} // namespace async_core::exec