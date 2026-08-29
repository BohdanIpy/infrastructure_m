#include <iostream>
#include <memory>
#include <coroutine>

import NetworkCoreLibrary;
import AsyncCoreLibrary;

using network_core::dns_resolution::DnsResolver;
using async_core::io::IoQueue;
using async_core::coro_loop::ICoroScheduler;
using async_core::coro_loop::CoroScheduler;
using async_core::exec::Task;
using async_core::sync::sync_wait;
using async_core::time::TimerQueue;

Task<void> resolve(DnsResolver& res)
{
    try
    {
        auto result = co_await res.Resolve("example.com");
        for (const auto& it : result)
        {
            std::cout << it.toString() << std::endl;
        }
        std::cout << "End of resolving\n";
    }
    catch (...)
    {
        std::cout << "Catched\n";
    }
}

int main(int argc, char *argv[])
{
    auto scheduler = std::make_unique<CoroScheduler>(4);
    auto timer_queue = std::make_unique<TimerQueue>();
    IoQueue queue{scheduler.get()};
    {
        DnsResolver resolver{queue, *timer_queue};
        sync_wait(resolve(resolver));
    }
    scheduler->stop_and_wait_all();
    return 0;
}