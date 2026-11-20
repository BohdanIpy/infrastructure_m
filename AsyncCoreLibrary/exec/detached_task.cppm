module;

#include <memory>
#include <utility>

export module AsyncCoreLibrary:DetachedTask;

import :SafeCoroutineHandle;
import :BasicPromiseType;
import :StartEndPolicy;
import :TaskBase;
import :TaskTypes;

namespace async_core::exec
{
    using common::SafeCoroutineHandle;
    using policy::EagerStartPolicy;
    using policy::AutoDestroyEndPolicy;
    using common::DetachedTaskES;
}

export namespace async_core::exec
{
    class BasicDetachedTask : public
        TaskBase
        <
            DetachedTaskES
        >
    {
    public:
        using promise_type = BasicPromiseType<
            BasicDetachedTask,
            void,
            TaskType::Detached,
            EagerStartPolicy,
            AutoDestroyEndPolicy
        >;

        using Base = TaskBase;

        explicit BasicDetachedTask(SafeCoroutineHandle handle)
            : Base(std::move(handle))
        {
        }

        ~BasicDetachedTask() = default;
    };

    using DetachedTask = BasicDetachedTask;
}
