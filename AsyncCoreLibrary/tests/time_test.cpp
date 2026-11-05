#include "gtest/gtest.h"

#include <chrono>
#include <coroutine>
#include <thread>

import AsyncCoreLibrary;

using namespace async_core::sync;
using namespace async_core::exec;
using namespace async_core::coro_loop;
using namespace async_core::time;
using namespace std::chrono_literals;
using namespace async_core::common;

using std::chrono::milliseconds;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

static Task<void> slf(TimerQueue& queue, const milliseconds& delay)
{
    co_await SleepFor{queue, delay};
}

static Task<void> slu(TimerQueue& queue, const milliseconds& delay)
{
    co_await SleepUntil{queue, Clock::now() + delay};
}

struct TimingBounds {
    milliseconds target;
    milliseconds min;
    milliseconds max;
};

constexpr TimingBounds CalculateBounds(milliseconds delay)
{
    auto min_dur = delay - std::clamp(delay / 10, 20ms, 100ms);

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    // Sanitizer overhead grows proportionally with long sleeps + fixed spin-up buffer
    auto max_dur = delay + (delay / 2) + 2000ms;
#  else
    auto max_dur = delay + (delay / 5) + 100ms; // Normal build: +20% + 100ms
#  endif
#else
    auto max_dur = delay + (delay / 5) + 100ms;
#endif

    return TimingBounds{ .target = delay, .min = min_dur, .max = max_dur };
}

enum class SleepType { SleepFor, SleepUntil };

struct SleepTestConfig {
    SleepType type;
    milliseconds delay;

    std::string GetName() const {
        std::string type_str = (type == SleepType::SleepFor) ? "SleepFor" : "SleepUntil";
        return type_str + "_" + std::to_string(delay.count()) + "ms";
    }
};

class TimerQueueMultiTimingTest : public ::testing::TestWithParam<SleepTestConfig> {};

TEST_P(TimerQueueMultiTimingTest, ValidatesDelayWindow)
{
    const auto [type, delay] = GetParam();
    const auto bounds = CalculateBounds(delay);

    TimerQueue queue{};

    auto start = Clock::now();

    if (type == SleepType::SleepFor) {
        sync_wait(slf(queue, delay));
    } else {
        sync_wait(slu(queue, delay));
    }

    auto end = Clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "[" << GetParam().GetName() << "] Elapsed: " << duration.count()
              << "ms (Expected: " << delay.count() << "ms)\n";

    EXPECT_GE(duration, bounds.min)
        << "Finished too early! Expected >= " << bounds.min.count() << "ms, got: " << duration.count() << "ms";

    EXPECT_LE(duration, bounds.max)
        << "Finished too late! Expected <= " << bounds.max.count() << "ms, got: " << duration.count() << "ms";
}

INSTANTIATE_TEST_SUITE_P(
    MultiTimingExecution, // name
    TimerQueueMultiTimingTest, // class for test_p
    ::testing::Values(
        SleepTestConfig{SleepType::SleepFor, 100ms},
        SleepTestConfig{SleepType::SleepFor, 200ms},
        SleepTestConfig{SleepType::SleepFor, 500ms},
        SleepTestConfig{SleepType::SleepFor, 1000ms},
        SleepTestConfig{SleepType::SleepUntil, 100ms},
        SleepTestConfig{SleepType::SleepUntil, 500ms}
    ),
    [](const ::testing::TestParamInfo<SleepTestConfig>& info) {
        return info.param.GetName();
    }
);

// ====================================

Task<int> FastTask(TimerQueue& queue) {
    co_await SleepFor(queue, 50ms);
    co_return 42;
}

Task<int> SlowTask(TimerQueue& queue) {
    co_await SleepFor(queue, 500ms);
    co_return 42;
}

TEST(Exec, TimeoutAfter1TaskCompletesInTime) {
    TimerQueue queue;
    auto scheduler = std::make_unique<CoroScheduler>(4);

    sync_wait([&]() -> Task<void> {
        auto t = FastTask(queue);
        auto result = co_await TimeoutAfter(queue, *scheduler, std::move(t), 5000ms);
        EXPECT_EQ(result, 42);
    }());
}

TEST(Exec, TimeoutAfter1TaskDoesNotCompleteInTime) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    TimerQueue queue;

    sync_wait([&]() -> Task<void> {
        EXPECT_THROW(co_await TimeoutAfter(queue, *scheduler, SlowTask(queue), 30ms), TimeoutException);
    }());
}

// ====================================

Task<void> FastTaskVoid(TimerQueue& queue) {
    co_await SleepFor(queue, 50ms);
}

Task<void> SlowTaskVoid(TimerQueue& queue) {
    co_await SleepFor(queue, 500ms);
}

TEST(Exec, TimeoutAfter1VoidTaskCompletesInTime)
{
    auto scheduler = std::make_unique<CoroScheduler>(4);
    TimerQueue queue;

    sync_wait([&]() -> Task<void>
    {
        auto t = FastTaskVoid(queue);
        co_await TimeoutAfter(queue, *scheduler, std::move(t), 500ms);
    }());
}

//====================

TEST(Exec, TimeoutAfter1VoidTaskDoesNotCompleteInTime) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    TimerQueue queue;

    sync_wait([&]() -> Task<void> {
        EXPECT_THROW(co_await TimeoutAfter(queue, *scheduler, SlowTaskVoid(queue), 30ms), TimeoutException);
    }());
}

// ====================================

// Cancelation

TEST(Exec, ImmediateCancelation)
{
    TimerQueue queue;
    bool was_successful = true;

    auto coro_fn = [&]() -> Task<void> {
        was_successful = co_await SleepFor(queue, 500ms);
    };

    auto task = coro_fn();

    task.cancel();
    sync_wait(std::move(task));
    EXPECT_FALSE(was_successful);
}

TEST(Exec, CancelMidFlightResumesCoroutine)
{
    TimerQueue queue;
    bool sleep_result = true;
    bool coroutine_completed = false;

    auto test_coro = [&]() -> Task<void> {
        sleep_result = co_await SleepFor(queue, 10s);
        coroutine_completed = true;
    };

    Task<void> task = test_coro();

    std::thread worker([&task] {
        sync_wait(task);
    });

    std::this_thread::sleep_for(10ms);
    EXPECT_FALSE(coroutine_completed);

    task.cancel();

    worker.join();

    EXPECT_TRUE(coroutine_completed);
    EXPECT_FALSE(sleep_result);
}
