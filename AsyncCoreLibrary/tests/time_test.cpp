#include "gtest/gtest.h"

#include <chrono>
#include <coroutine>

import AsyncCoreLibrary;

using namespace async_core::sync;
using namespace async_core::exec;
using namespace async_core::coro_loop;
using namespace async_core::time;
using namespace std::chrono_literals;

using std::chrono::milliseconds;

Task<void> slf(TimerQueue& queue)
{
    co_await SleepFor{queue, std::chrono::milliseconds(700)};
}

TEST(Exec, SleepFor)
{
    TimerQueue queue{};
    auto start = Clock::now();
    sync_wait(std::move(slf(queue)));
    auto end = Clock::now();
    auto duration = std::chrono::duration_cast<milliseconds>(end - start);
    std::cout << duration << "\n";
    ASSERT_GT(duration, std::chrono::milliseconds(650));
    ASSERT_LT(duration, std::chrono::milliseconds(750));
}

// ====================================

Task<void> slu(TimerQueue& queue)
{
    co_await SleepUntil{queue, Clock::now() + std::chrono::milliseconds(300)};
}

TEST(Exec, SleepUntil)
{
    TimerQueue queue{};
    auto start = Clock::now();
    sync_wait(std::move(slu(queue)));
    auto end = Clock::now();
    auto duration = std::chrono::duration_cast<milliseconds>(end - start);
    std::cout << duration << "\n";
    ASSERT_GT(duration, std::chrono::milliseconds(270));
    ASSERT_LT(duration, std::chrono::milliseconds(330));
}

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
    auto scheduler = std::make_unique<CoroScheduler>(4);

    TimerQueue queue;

    sync_wait([&]() -> Task<void> {
        auto t = FastTask(queue);
        auto result = co_await TimeoutAfter(queue, scheduler.get(), std::move(t), 5000ms);
        EXPECT_EQ(result, 42);
    }());
}

TEST(Exec, TimeoutAfter1TaskDoesNotCompleteInTime) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    TimerQueue queue;

    sync_wait([&]() -> Task<void> {
        EXPECT_THROW(co_await TimeoutAfter(queue, scheduler.get(), SlowTask(queue), 30ms), TimeoutException);
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
        co_await TimeoutAfter(queue, scheduler.get(), std::move(t), 500ms);
    }());
}

TEST(Exec, TimeoutAfter1VoidTaskDoesNotCompleteInTime) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    TimerQueue queue;

    sync_wait([&]() -> Task<void> {
        EXPECT_THROW(co_await TimeoutAfter(queue, scheduler.get(), SlowTaskVoid(queue), 30ms), TimeoutException);
    }());
}

// ====================================

