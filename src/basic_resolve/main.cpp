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

Task<void> resolve(DnsResolver& res)
{
    auto result = co_await res.resolve("www.example.com");
    for (const auto& it : result)
    {
        std::cout << it.toString() << std::endl;
    }
    std::cout << "End of resolving\n";
}

int main(int argc, char *argv[])
{
    std::unique_ptr<ICoroScheduler> scheduler = std::make_unique<CoroScheduler>(4);
    IoQueue queue{scheduler.get()};
    DnsResolver resolver{queue};
    sync_wait(resolve(resolver));
    return 0;
}