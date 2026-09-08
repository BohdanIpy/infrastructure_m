#include "gtest/gtest.h"

#include <latch>
#include <coroutine>

import AsyncCoreLibrary;

using namespace async_core::sync;
using namespace async_core::exec;
using namespace async_core::coro_loop;


TEST(AsyncChannelTest, BasicBufferedSendReceiveFIFO) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncChannel<int> chan(*scheduler,3);

    auto producer = [&]() -> DetachedTask {
        co_await chan.send(10);
        co_await chan.send(20);
        co_await chan.send(30);
    };

    producer();

    std::vector<int> received;
    auto consumer = [&]() -> DetachedTask {
        while (auto val = co_await chan.receive()) {
            received.push_back(*val);
            if (received.size() == 3) {
                chan.close();
            }
        }
    };

    consumer();

    EXPECT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], 10);
    EXPECT_EQ(received[1], 20);
    EXPECT_EQ(received[2], 30);
}

TEST(AsyncChannelTest, DirectHandoffRendezvous) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncChannel<std::string> chan(*scheduler, 0);
    std::string received_msg;

    std::latch completion_latch{1};

    auto consumer = [&]() -> DetachedTask {
        auto res = co_await chan.receive();
        if (res) received_msg = *res;
        completion_latch.count_down();
    };

    consumer();
    EXPECT_TRUE(received_msg.empty());

    auto producer = [&]() -> DetachedTask {
        co_await chan.send("Hello Coroutine!");
    };

    producer();

    completion_latch.wait();

    EXPECT_EQ(received_msg, "Hello Coroutine!");
}

TEST(AsyncChannelTest, ClosedChannelReturnsNullopt) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncChannel<int> chan(*scheduler, 1);

    std::atomic<bool> consumer_saw_nullopt{false};
    std::latch consumer_latch{1};

    auto consumer = [&]() -> DetachedTask {
        auto val = co_await chan.receive();
        if (!val.has_value()) {
            consumer_saw_nullopt = true;
        }
        consumer_latch.count_down();
    };

    consumer();
    EXPECT_FALSE(consumer_saw_nullopt.load());

    chan.close();

    consumer_latch.wait();
    EXPECT_TRUE(consumer_saw_nullopt.load());

    std::atomic<bool> send_result{true};
    std::latch producer_latch{1};

    auto producer = [&]() -> DetachedTask {
        send_result = co_await chan.send(42);
        producer_latch.count_down();
    };

    producer();

    producer_latch.wait();
    EXPECT_FALSE(send_result.load());
}

TEST(AsyncChannelTest, DrainBufferAfterClose) {
    auto scheduler = std::make_unique<CoroScheduler>(4);
    AsyncChannel<int> chan(*scheduler,2);

    auto producer = [&]() -> DetachedTask {
        co_await chan.send(100);
        co_await chan.send(200);
        chan.close();
    };

    producer();

    std::vector<int> received;
    auto consumer = [&]() -> DetachedTask {
        while (auto val = co_await chan.receive()) {
            received.push_back(*val);
        }
    };

    consumer();

    EXPECT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], 100);
    EXPECT_EQ(received[1], 200);
}

TEST(AsyncChannelTest, MultiProducerSingleConsumerStress) {
    for (int i = 0; i < 200; ++i)
    {
        constexpr int kNumProducers = 4;
        constexpr int kItemsPerProducer = 250;
        constexpr int kExpectedTotal = kNumProducers * kItemsPerProducer;

        auto scheduler = std::make_unique<CoroScheduler>(8);
        AsyncChannel<int> chan(*scheduler, 16);

        std::atomic<int> total_received{0};
        std::atomic<int> sum_received{0};

        std::latch producers_latch{kNumProducers};
        std::latch consumer_latch{1};

        // Capture references explicitly or pass pointers to ensure lifetime safety
        auto consumer_task = [](AsyncChannel<int>& ch,
                                std::atomic<int>& total,
                                std::atomic<int>& sum,
                                std::latch& latch) -> DetachedTask
        {
            while (auto val = co_await ch.receive()) {
                total.fetch_add(1, std::memory_order_relaxed);
                sum.fetch_add(*val, std::memory_order_relaxed);
            }
            latch.count_down();
        };

        // Start Consumer Coroutine
        consumer_task(chan, total_received, sum_received, consumer_latch);

        // Start Producer Coroutines
        for (int p = 0; p < kNumProducers; ++p) {
            auto producer_task = [](AsyncChannel<int>& ch,
                                    std::latch& latch) -> DetachedTask
            {
                for (int j = 0; j < kItemsPerProducer; ++j) {
                    auto sd = co_await ch.send(1);
                    if (!sd) {
                        break;
                    }
                }
                latch.count_down();
            };

            producer_task(chan, producers_latch);
        }

        // Monitor thread waits for producers to finish, then closes channel
        std::thread monitor([&producers_latch, &chan]() {
            producers_latch.wait();
            chan.close();
        });

        // Wait for consumer to process all values and terminate on close
        consumer_latch.wait();

        if (monitor.joinable()) {
            monitor.join();
        }

        chan.dump_diagnostics();

        EXPECT_EQ(total_received.load(), kExpectedTotal);
        EXPECT_EQ(sum_received.load(), kExpectedTotal);
    }
}