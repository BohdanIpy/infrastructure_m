#include <gtest/gtest.h>

#include <coroutine>

import AsyncCoreLibrary;

using namespace async_core::sync;
using namespace async_core::exec;
using namespace async_core::coro_loop;
using namespace async_core::context;

class SleepForOld
{
    std::chrono::milliseconds m_waitTime;

public:
    explicit SleepForOld(std::chrono::milliseconds wait) : m_waitTime(wait) {}

    [[nodiscard]]
    bool await_ready() const noexcept
    {
        return m_waitTime <= std::chrono::milliseconds::zero();
    }

    void await_suspend(std::coroutine_handle<> h) const
    {
        std::thread thread{[h, waitTime = m_waitTime]()
        {
            std::this_thread::sleep_for(waitTime);
            h.resume();
        }};
        thread.detach();
    }

    void await_resume() const noexcept {}
};

Task<int> GetBaseNumber() {
    co_return 10;
}

Task<int> ComputeResult() {
    int base = co_await GetBaseNumber();
    co_return base * 4 + 2;
}

Task<int> AsyncAdd(int a, int b) {
    // Simulate background work
    co_await SleepForOld{std::chrono::milliseconds(200)};
    co_return a + b;
}

Task<int> Calculate() {
    int x = co_await AsyncAdd(10, 20);
    int y = co_await AsyncAdd(x, 12);
    co_return y;
}

Task<void> LogMessageAsync(std::string msg) {
    co_await SleepForOld{std::chrono::milliseconds(100)};
    std::cout << "[Log] " << msg << "\n";
    co_return; // void return!
}

Task<void> DoWork() {
    co_await LogMessageAsync("Task 1 started...");
    co_await LogMessageAsync("Task 2 running...");
    co_await LogMessageAsync("All work finished!");
}

Task<void> SomeVoidTask() { co_return; }

Task<void> ParentTask() {
    co_await SomeVoidTask();
}

LazyTask<void> ExecuteThreadHop() {
    std::thread::id idFirst =  std::this_thread::get_id();

    co_await SwitchToThread{};

    std::thread::id idSecond = std::this_thread::get_id();

    EXPECT_NE(idFirst, idSecond);
}

TEST(Exec,ThreadHop)
{
    auto hop = ExecuteThreadHop();
    sync_wait(std::move(hop));
}

Generator<int> CountTo(int max) {
    for (int i = 1; i <= max; ++i) {
        co_yield i;
    }
}

Generator<int> ThrowingGenerator() {
    co_yield 100;
    throw std::runtime_error("Generator failed!");
}


Generator<int> CountUp(int start, int end) {
    for (int i = start; i <= end; ++i) {
        co_yield i; // Yields a value and suspends
    }
}

TEST(Exec, Generator)
{
    int i1 = 1;
    auto gen = CountTo(3);
    while (gen.move_next()) {
        ASSERT_EQ(i1, gen.current_value());
        ++i1;
    }

    auto errGen = ThrowingGenerator();
    ASSERT_TRUE(errGen.move_next());
    ASSERT_EQ(errGen.current_value(), 100);
    ASSERT_THROW(errGen.move_next(), std::runtime_error);
}

Generator<int> Fibonacci(int max_count) {
    int a = 0, b = 1;
    for (int i = 0; i < max_count; ++i) {
        co_yield a;
        auto next = a + b;
        a = b;
        b = next;
    }
}

TEST(Exec, Iteration)
{
    int i1 = 0;
    int arr1[] = {1, 2, 3, 4, 5};
    int i2 = 0;
    int arr2[] = {0, 1, 1, 2, 3, 5, 8, 13};
    for (int val : CountUp(1, 5)) {
        ASSERT_EQ(val, arr1[i1]);
        ++i1;
    }
    for (int val : Fibonacci(8)) {
        ASSERT_EQ(val, arr2[i2]);  // Output: 0 1 1 2 3 5 8 13
        ++i2;
    }
}

TEST(Exec, TaskSymmetricHandling)
{
    auto t = ComputeResult();
    const auto res = sync_wait(std::move(t));
    ASSERT_EQ(res, 42);

    auto t2 = Calculate();
    const auto res2 = sync_wait(std::move(t2));
    ASSERT_EQ(res2, 42);
}
