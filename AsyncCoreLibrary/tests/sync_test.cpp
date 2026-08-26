#include "gtest/gtest.h"

#include <coroutine>
#include <chrono>
#include <memory>
#include <latch>

import AsyncCoreLibrary;

using namespace async_core::sync;
using namespace async_core::exec;
using namespace async_core::coro_loop;

using namespace std::chrono_literals;

Task<void> TestBasicSignal(AsyncAutoResetEvent& event, bool& resumed) {
    co_await event;
    resumed = true;
}

TEST(Exec, AsyncAutoResetEvent) {
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(2);
    AsyncAutoResetEvent event{scheduler.get(), false};
    bool resumed = false;

    std::thread producer([&event]() {
        std::this_thread::sleep_for(100ms);
        event.set();
    });

    sync_wait(TestBasicSignal(event, resumed));

    if (producer.joinable()) {
        producer.join();
    }

    EXPECT_TRUE(resumed);
}

// ====================================

Task<void> Worker(AsyncAutoResetEvent& event, int id, std::vector<int>& execution_order) {
    co_await event;
    execution_order.push_back(id);
}

Task<void> TestAutoResetSingleUnblock(AsyncAutoResetEvent& event, std::vector<int>& order) {
    std::this_thread::sleep_for(50ms);

    // Unblocks first worker
    event.set();
    std::this_thread::sleep_for(50ms);

    // Unblocks second worker
    event.set();
    std::this_thread::sleep_for(50ms);

    co_return;
}

TEST(Exec, UnblockAsyncAutoResetEvent)
{
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(2);
    AsyncAutoResetEvent event{scheduler.get(), false};
    std::vector<int> order;

    auto w1 = Worker(event, 1, order);
    auto w2 = Worker(event, 2, order);

    // Spawn workers on separate threads
    std::thread t1([&]() { sync_wait(std::move(w1)); });
    std::thread t2([&]() { sync_wait(std::move(w2)); });

    // Trigger event sequence
    auto t = TestAutoResetSingleUnblock(event, order);
    sync_wait(std::move(t));

    // SAFELY join threads on the main test thread AFTER sync_wait completes!
    if (t1.joinable()) t1.join();
    if (t2.joinable()) t2.join();

    EXPECT_EQ(order.size(), 2);
}

// ====================================

Task<void> TestPreSignaled(AsyncAutoResetEvent& event, const std::atomic<bool>& second_finished) {
    event.set();

    co_await event;

    co_await event;

    EXPECT_TRUE(second_finished.load());
}

TEST(Exec, PreSignaled)
{
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(2);
    AsyncAutoResetEvent event{scheduler.get(), false};

    std::atomic<bool> second_finished{false};

    std::thread background_setter([&]() {
        std::this_thread::sleep_for(50ms);
        second_finished.store(true, std::memory_order_release);
        event.set();
    });

    auto t = TestPreSignaled(event, second_finished);
    sync_wait(std::move(t));

    if (background_setter.joinable()) {
        background_setter.join();
    }
}

// ====================================

TEST(AsyncManualResetEventTest, PreSignaledEventDoesNotSuspend)
{
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(2);
    AsyncManualResetEvent event{scheduler.get(), true};

    bool finished = false;
    auto test_task = [&]() -> DetachedTask {
        co_await event;
        finished = true;
    };

    test_task();
    EXPECT_TRUE(finished);
}

// ====================================

TEST(AsyncManualResetEventTest, SignalResumesWaitingCoroutine) {
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(2);
    AsyncManualResetEvent event{scheduler.get(), false};
    std::atomic<bool> resumed{false};
    std::latch done_latch{1};

    auto waiter = [&]() -> DetachedTask {
        co_await event;
        resumed = true;
        done_latch.count_down();
    };

    waiter();
    EXPECT_FALSE(resumed);

    std::thread producer([&]() {
        std::this_thread::sleep_for(10ms);
        event.set();
    });
    producer.join();

    done_latch.wait();
    EXPECT_TRUE(resumed);
}

// ====================================

TEST(AsyncManualResetEventTest, SetResumesMultipleWaiters) {
    auto scheduler = std::make_unique<CoroScheduler>(2);
    AsyncManualResetEvent event{scheduler.get(), false};
    constexpr int kNumWaiters = 5;
    std::atomic<int> resumed_count{0};

    auto create_waiter = [&]() -> DetachedTask {
        co_await event;
        resumed_count.fetch_add(1, std::memory_order_relaxed);
    };

    for (int i = 0; i < kNumWaiters; ++i) {
        create_waiter();
    }

    EXPECT_EQ(resumed_count.load(), 0);

    event.set();

    auto start = std::chrono::steady_clock::now();
    while (resumed_count.load() < kNumWaiters) {
        std::this_thread::yield();
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) {
            FAIL() << "Timed out waiting for waiters to resume";
        }
    }

    EXPECT_EQ(resumed_count.load(), kNumWaiters);
}

// ====================================

TEST(AsyncManualResetEventTest, ResetForcesNextWaitersToSuspend) {
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(2);
    AsyncManualResetEvent event{scheduler.get(), false};
    std::atomic<int> completed_phase{0};

    std::latch latch1{1};
    auto waiter1 = [&]() -> DetachedTask {
        co_await event;
        completed_phase = 1;
        latch1.count_down();
    };

    waiter1();
    EXPECT_EQ(completed_phase.load(), 0);

    event.set();
    latch1.wait();
    EXPECT_EQ(completed_phase.load(), 1);

    event.reset();

    std::latch latch2{1};
    auto waiter2 = [&]() -> DetachedTask {
        co_await event;
        completed_phase = 2;
        latch2.count_down();
    };

    waiter2();
    EXPECT_EQ(completed_phase.load(), 1);

    event.set();
    latch2.wait();
    EXPECT_EQ(completed_phase.load(), 2);
}

// ====================================

TEST(Exec, TryLockBasic)
{
    AsyncMutex mtx;

    EXPECT_TRUE(mtx.try_lock());
    EXPECT_FALSE(mtx.try_lock());

    mtx.unlock();
    EXPECT_TRUE(mtx.try_lock());
    mtx.unlock();
}

// ====================================

TEST(Exec, ScopedLockAutoReleases) {
    AsyncMutex mtx;
    bool executed_critical_section = false;

    auto task = [&]() -> DetachedTask {
        {
            auto lock = co_await mtx.lock_async();
            executed_critical_section = true;
        }
    };

    task();
    EXPECT_TRUE(executed_critical_section);

    EXPECT_TRUE(mtx.try_lock());
    mtx.unlock();
}

// ====================================

TEST(AsyncMutexTest, GuaranteesMutualExclusion) {
    auto scheduler = std::make_unique<CoroScheduler>(4);

    AsyncMutex mtx{scheduler.get()};

    int shared_counter = 0;
    std::atomic<int> active_in_critical_section{0};
    bool max_concurrency_exceeded = false;

    constexpr int kNumTasks = 8;
    constexpr int kIncrementsPerTask = 50;

    std::latch completion_latch{kNumTasks};

    auto worker = [&]() -> DetachedTask {
        for (int i = 0; i < kIncrementsPerTask; ++i) {
            auto lock = co_await mtx.lock_async();

            int active = active_in_critical_section.fetch_add(1, std::memory_order_relaxed) + 1;
            if (active > 1) {
                max_concurrency_exceeded = true;
            }

            int current = shared_counter;
            std::this_thread::yield();
            shared_counter = current + 1;

            active_in_critical_section.fetch_sub(1, std::memory_order_relaxed);
        }
        completion_latch.count_down();
    };

    for (int i = 0; i < kNumTasks; ++i) {
        worker();
    }

    completion_latch.wait();

    EXPECT_FALSE(max_concurrency_exceeded) << "More than 1 coroutine entered critical section!";
    EXPECT_EQ(shared_counter, kNumTasks * kIncrementsPerTask);
}

// ====================================

TEST(AsyncMutexTest, SequentialLockHandoff) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncMutex mtx{scheduler.get()};

    std::vector<int> execution_order;
    std::mutex order_mtx;
    std::latch completion_latch{3};

    auto create_waiter = [&](int id) -> DetachedTask {
        auto lock = co_await mtx.lock_async();

        {
            std::lock_guard lk(order_mtx);
            execution_order.push_back(id);
        }

        completion_latch.count_down();
    };

    EXPECT_TRUE(mtx.try_lock());

    create_waiter(1);
    create_waiter(2);
    create_waiter(3);

    {
        std::lock_guard lk(order_mtx);
        EXPECT_TRUE(execution_order.empty());
    }

    mtx.unlock();

    completion_latch.wait();

    std::lock_guard lk(order_mtx);
    ASSERT_EQ(execution_order.size(), 3u);
    EXPECT_EQ(execution_order[0], 1);
    EXPECT_EQ(execution_order[1], 2);
    EXPECT_EQ(execution_order[2], 3);
}

// ====================================

TEST(AsyncSemaphoreTest, TryAcquireBasic) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncSemaphore sem(scheduler.get(), 2);

    EXPECT_TRUE(sem.try_acquire());
    EXPECT_TRUE(sem.try_acquire());
    EXPECT_FALSE(sem.try_acquire());

    sem.release();
    EXPECT_TRUE(sem.try_acquire());
}

// ====================================

TEST(AsyncSemaphoreTest, EnforcesMaxConcurrencyLimit) {
    constexpr size_t kMaxPermits = 3;
    constexpr int kTotalTasks = 10;

    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncSemaphore sem(scheduler.get(), kMaxPermits);
    std::atomic<int> active_coroutines{0};
    std::atomic<bool> limit_exceeded{false};
    std::atomic<int> completed_tasks{0};

    std::latch completion_latch{kTotalTasks};

    auto worker = [&]() -> DetachedTask {
        co_await sem.acquire_async();

        int current = active_coroutines.fetch_add(1, std::memory_order_relaxed) + 1;
        if (current > static_cast<int>(kMaxPermits)) {
            limit_exceeded = true;
        }

        std::this_thread::sleep_for(5ms);

        active_coroutines.fetch_sub(1, std::memory_order_relaxed);
        sem.release();

        completed_tasks.fetch_add(1, std::memory_order_relaxed);
        completion_latch.count_down();
    };

    for (int i = 0; i < kTotalTasks; ++i) {
        worker();
    }

    completion_latch.wait();

    EXPECT_FALSE(limit_exceeded) << "More coroutines ran than permitted by the semaphore!";
    EXPECT_EQ(completed_tasks.load(), kTotalTasks);
}

// ====================================

TEST(AsyncSemaphoreTest, QueueingAndHandoff) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncSemaphore sem(scheduler.get(), 0);

    std::vector<int> execution_order;
    std::mutex order_mtx;

    std::latch step1_latch{1};
    std::latch step2_latch{1};
    std::latch step3_latch{1};

    auto waiter = [&](int id, std::latch& step_latch) -> DetachedTask {
        co_await sem.acquire_async();
        {
            std::lock_guard lk(order_mtx);
            execution_order.push_back(id);
        }
        step_latch.count_down();
    };

    waiter(1, step1_latch);
    waiter(2, step2_latch);
    waiter(3, step3_latch);

    {
        std::lock_guard lk(order_mtx);
        EXPECT_TRUE(execution_order.empty());
    }

    sem.release();
    step1_latch.wait();
    {
        std::lock_guard lk(order_mtx);
        ASSERT_EQ(execution_order.size(), 1u);
        EXPECT_EQ(execution_order[0], 1);
    }

    sem.release();
    step2_latch.wait();
    {
        std::lock_guard lk(order_mtx);
        ASSERT_EQ(execution_order.size(), 2u);
        EXPECT_EQ(execution_order[1], 2);
    }

    sem.release();
    step3_latch.wait();
    {
        std::lock_guard lk(order_mtx);
        ASSERT_EQ(execution_order.size(), 3u);
        EXPECT_EQ(execution_order[2], 3);
    }
}

// ====================================

TEST(AsyncSemaphoreTest, ProducerConsumerSignaling) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncSemaphore sem(scheduler.get(), 0);
    std::atomic<bool> producer_done{false};
    std::atomic<bool> consumer_done{false};

    std::latch consumer_latch{1};

    auto consumer = [&]() -> DetachedTask {
        co_await sem.acquire_async();
        EXPECT_TRUE(producer_done.load());
        consumer_done = true;
        consumer_latch.count_down();
    };

    consumer();
    EXPECT_FALSE(consumer_done.load());

    std::thread producer([&]() {
        std::this_thread::sleep_for(10ms);
        producer_done = true;
        sem.release();
    });

    producer.join();

    consumer_latch.wait();

    EXPECT_TRUE(consumer_done.load());
}

// ====================================

TEST(WhenAllTest, BasicTupleResult) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    auto fetch_num = []() -> Task<int> { co_return 42; };
    auto fetch_str = []() -> Task<std::string> { co_return "hello"; };

    auto main_task = [&]() -> DetachedTask {
        auto [num, str] = co_await when_all_on(*scheduler,fetch_num(), fetch_str());

        EXPECT_EQ(num, 42);
        EXPECT_EQ(str, "hello");
    };

    main_task();
}