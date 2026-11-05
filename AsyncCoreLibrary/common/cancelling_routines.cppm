module;

#include <type_traits>
#include <stop_token>
#include <functional>
#include <mutex>
#include <optional>

export module AsyncCoreLibrary:CancellingRoutines;

export namespace async_core::common
{
    template <typename T, typename = void>
    struct has_get_stop_token : std::false_type {};

    template <typename T>
    struct has_get_stop_token<T, std::void_t<decltype(std::declval<T>().get_stop_token())>>
        : std::is_same<decltype(std::declval<T>().get_stop_token()), std::stop_token> {};

    template <typename T>
    inline constexpr bool has_get_stop_token_v = has_get_stop_token<T>::value;

    struct CoroutineNode {
        CoroutineNode* parent{nullptr};
        CoroutineNode* first_child{nullptr};
        CoroutineNode* next_sibling{nullptr};
        CoroutineNode* prev_sibling{nullptr};

        void add_child(CoroutineNode* child) noexcept {
            child->parent = this;
            child->next_sibling = first_child;
            if (first_child) {
                first_child->prev_sibling = child;
            }
            first_child = child;
        }

        void remove_from_parent() noexcept {
            if (prev_sibling) {
                prev_sibling->next_sibling = next_sibling;
            } else if (parent && parent->first_child == this) {
                parent->first_child = next_sibling;
            }

            if (next_sibling) {
                next_sibling->prev_sibling = prev_sibling;
            }

            parent = nullptr;
            next_sibling = nullptr;
            prev_sibling = nullptr;
        }
    };

    struct CompactInvoker {
        void(*fn)(void*) noexcept{nullptr};
        void* ctx{nullptr};

        void operator()() const noexcept {
            if (fn && ctx) fn(ctx);
        }
    };

    class IPromiseStopControl {
    public:
        virtual ~IPromiseStopControl() = default;
        virtual void request_stop() noexcept = 0;
    };

    class CancellablePromiseMixin
    {
        std::mutex m_callback_mtx;
        std::optional<std::stop_callback<CompactInvoker>> m_cancellation_node;

    public:
        CancellablePromiseMixin() = default;

        void set_cancellation_callback(std::stop_token token, void(*fn)(void*) noexcept, void* ctx)
        {
            std::lock_guard lock(m_callback_mtx);
            if (token.stop_possible()) {
                m_cancellation_node.emplace(token, CompactInvoker{ .fn = fn, .ctx = ctx });
            }
        }

        void clear_cancellation_callback() noexcept
        {
            std::lock_guard lock(m_callback_mtx);
            m_cancellation_node.reset();
        }
    };

}