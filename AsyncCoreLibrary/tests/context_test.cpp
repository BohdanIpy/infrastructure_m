#include "gtest/gtest.h"

#include <coroutine>
#include <thread>

import AsyncCoreLibrary;

using namespace async_core::exec;
using namespace async_core::context;
using async_core::sync::sync_wait;

Task<void> changing(std::thread::id& before_id, std::thread::id& after_id)
{
    before_id = std::this_thread::get_id();
    co_await SwitchToThread();
    after_id = std::this_thread::get_id();
}

TEST(Context, ThreadSwitching)
{
    std::thread::id before;
    std::thread::id after;
    sync_wait(changing(before, after));
    EXPECT_NE(before, after);
}
