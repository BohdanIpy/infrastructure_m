module;

#include <iostream>
#include <memory>
#include <utility>

export module AsyncCoreLibrary:DetachedTask;

import :SafeCoroutineHandle;
import :BasicPromiseType;
import :StartEndPolicy;
import :TaskBase;

using async_core::common::SafeCoroutineHandle;
using async_core::policy::EagerStartPolicy;
using async_core::policy::AutoDestroyEndPolicy;

export namespace async_core::exec
{
    template <typename StreamType = std::ostream&>
    class BasicDetachedTask : public TaskBase<
        BasicDetachedTask<StreamType>,
        BasicPromiseType<BasicDetachedTask<StreamType>, void, TaskType::Detached, EagerStartPolicy, AutoDestroyEndPolicy>
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

        using Base = TaskBase<BasicDetachedTask, promise_type>;

        explicit BasicDetachedTask(SafeCoroutineHandle handle)
            : Base(std::move(handle)) {}

        ~BasicDetachedTask() = default;
    };

    using DetachedTask = BasicDetachedTask<>;
}
