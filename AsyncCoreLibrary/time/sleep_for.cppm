module;

#include <type_traits>
#include <concepts>
#include <chrono>
#include <coroutine>

export module AsyncCoreLibrary:SleepFor;

import :TimerQueue;

using namespace std::chrono;

export namespace async_core::time
{
    using Clock = steady_clock;

    template <typename T>
    struct is_duration : std::false_type {};

    template <typename Rep, typename Period>
    struct is_duration<duration<Rep, Period>> : std::true_type {};

    template <typename T>
    inline constexpr bool is_duration_v = is_duration<T>::value;

    template <typename T>
    concept DurationType = is_duration_v<std::remove_cvref_t<T>>;

    class SleepFor
    {
        TimerQueue& m_queue;
        milliseconds m_duration;

    public:
        template <DurationType DurationT>
        explicit SleepFor(TimerQueue& queue, const DurationT& duration) :
            m_queue(queue),
            m_duration(std::chrono::duration_cast<milliseconds>(duration)) {}

        bool await_ready() const noexcept
        {
            return m_duration <= milliseconds::zero();
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            m_queue.AddTimer(h, m_duration);
        }

        void await_resume() const noexcept {}
    };
}
