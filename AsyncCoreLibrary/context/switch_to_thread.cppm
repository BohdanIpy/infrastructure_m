module;

#include <thread>
#include <coroutine>

export module AsyncCoreLibrary:SwitchToThread;

export namespace async_core::context {
/**
 * @brief Class for deploying a coroutine on the separate std::thread
 */
class SwitchToThread {
  public:
    [[nodiscard]]
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) const {
        std::thread thread{[h]() { h.resume(); }};
        thread.detach();
    }

    void await_resume() const noexcept {}
};
} // namespace async_core::context
