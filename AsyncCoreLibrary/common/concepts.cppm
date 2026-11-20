module;

#include <concepts>
#include <stop_token>

export module AsyncCoreLibrary:Concepts;

export namespace async_core::common
{
    template <typename T>
    concept CancellableAwaiterConcept = requires(T t, std::stop_token st) {
        { t.register_cancellation(st) } -> std::same_as<void>;
    };

    template <typename T>
    concept HasGetStopTokenConcept = requires(T t)
    {
        {t.GetStopToken()} -> std::same_as<std::stop_token>;
    };

    template <typename T>
    concept HasRequestStopConcept = requires(T t)
    {
        {t.RequestStop()} -> std::same_as<void>;
    };

    class SafeCoroutineHandle;
    /**
     * Concept which used to enforce the template parameter to have a SafeCoroutineHandle - required for safe processing in library
     */
    template <typename Promise>
    concept HasSafeHandleConcept = requires {
        { std::declval<Promise&>().GetSafeHandle() } -> std::same_as<SafeCoroutineHandle>;
    };

    struct WiredTaskES;
    template <typename StructT>
    concept IsWiredTaskEsConcept = std::same_as<StructT, WiredTaskES>;

    struct DetachedTaskES;
    template <typename StructT>
    concept IsDetachedTaskEsConcept = std::same_as<StructT, DetachedTaskES>;

}
