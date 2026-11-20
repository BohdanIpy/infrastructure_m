module;

#include <stop_token>
#include <functional>

export module AsyncCoreLibrary:CancellingRoutines;

namespace async_core::common
{

}

export namespace async_core::common
{
    struct CancellationNode
    {
        std::stop_source m_source;

        template <typename Awaitable>
        Awaitable await_transform(Awaitable&& awaitable)
        {
            // if constexpr (!std::is_same_v<Awaitable::shield_type, /* shielded{} struct*/>)
            //     awaitable.InjectToken(m_source.get_token());
            return std::forward<Awaitable>(awaitable);
        }
    };
}
