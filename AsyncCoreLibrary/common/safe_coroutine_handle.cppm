module;

#include <atomic>
#include <coroutine>
#include <memory>
#include <mutex>
#include <stop_token>
#include <typeindex>
#include <typeinfo>
#include <utility>

export module AsyncCoreLibrary:SafeCoroutineHandle;

import :CoroutineStates;

export namespace async_core::common {
/**
 * @brief Shared state of one coroutine frame.
 *
 * Only three states are used by the logic below:
 *   Created   - the frame exists, nobody has started it yet
 *   Started   - started at least once, frame still alive. "Executing right now"
 * and "parked at an await point" are deliberately NOT distinguished: with
 * symmetric transfer the thread that called resume() is not the one that sees
 * the coroutine suspend, so such a flag would be stale and (worse) would make a
 * legitimate resume from another thread fail. Destroyed - terminal. raw_handle
 * is null and every transition out of it is ignored.
 *
 * Exactly-once resumption is guaranteed by the awaiter protocol (the
 * Init/Armed/Done/Cancelled state machine inside each awaiter), NOT by this
 * block. This block only guarantees that a dead frame is never touched.
 *
 * Thread-safety: raw_handle and every lifecycle transition are guarded by mtx.
 * The mutex is never held while user code runs (resume) or while a frame is
 * being destroyed, because the promise destructor re-enters set_state() on the
 * same block.
 */
struct CoroutineControlBlock {
    std::atomic<CoroutineState> m_state{CoroutineState::Created};
    std::coroutine_handle<> m_rawHandle{nullptr};
    std::type_index m_typeIdx;
    std::stop_source m_stopSource;
    mutable std::mutex m_mtx;

    explicit CoroutineControlBlock(std::coroutine_handle<> handle, std::type_index idx,
                                   std::stop_source src)
        : m_rawHandle(handle)
        , m_typeIdx(idx)
        , m_stopSource(std::move(src)) {}

    /**
     * @brief Called from the promise destructor (Destroyed) - idempotent, Destroyed is
     * terminal.
     */
    void SetStateInternal(CoroutineState s) {
        std::lock_guard lock(m_mtx);
        if (m_state.load(std::memory_order_relaxed) == CoroutineState::Destroyed) {
            return;
        }
        m_state.store(s, std::memory_order_release);
        if (s == CoroutineState::Destroyed) {
            m_rawHandle = nullptr;
        }
    }

    template <std::memory_order order = std::memory_order_acquire>
    CoroutineState LoadStateInternal() const {
        return m_state.load(order);
    }

    /**
     * @brief Marks the coroutine as started (Created -> Started). Idempotent.
     * @return false if the frame is already destroyed
     */
    bool MarkStartedInternal() {
        std::lock_guard lock(m_mtx);
        CoroutineState expected = CoroutineState::Created;
        m_state.compare_exchange_strong(expected, CoroutineState::Started,
                                        std::memory_order_acq_rel, std::memory_order_relaxed);
        return m_state.load(std::memory_order_relaxed) != CoroutineState::Destroyed;
    }

    /**
     * @brief Mark only once what coroutine was started, and after it executing
     * @return false if the frame is destroyed or sitting at final_suspend
     * (resuming that would be UB)
     */
    bool TryExecuteResumeInternal() {
        std::coroutine_handle<> h;
        {
            std::lock_guard lock(m_mtx);
            if (m_state.load(std::memory_order_relaxed) == CoroutineState::Destroyed ||
                !m_rawHandle || m_rawHandle.done()) {
                return false;
            }
            CoroutineState expected = CoroutineState::Created;
            m_state.compare_exchange_strong(expected, CoroutineState::Started,
                                            std::memory_order_acq_rel, std::memory_order_relaxed);
            h = m_rawHandle;
        }
        h.resume();
        return true;
    }

    /**
     * @brief Destroys the frame exactly once, whoever gets here first wins. Precondition
     * (ownership contract): the caller must make sure the coroutine is not
     * executing on another thread - cancel it and wait for completion.
     */
    bool TryExecuteDestroyInternal() { return DestroyFrameInternal(/*only_if_unstarted=*/false); }

    /**
     * @brief Destroys the frame only if it was never started.
     */
    bool TryExecuteDestroyUnstartedInternal() {
        return DestroyFrameInternal(/*only_if_unstarted=*/true);
    }

    /**
     * @brief Get the Raw Handle Internal object
     *
     * @tparam PromiseT type of coroutine_handle we want to get.
     * @return std::coroutine_handle<PromiseT> Returns a parametrized version, which is important
     * for us
     */
    template <typename PromiseT> std::coroutine_handle<PromiseT> GetRawHandleInternal() const {
#ifndef NDEBUG
        if (std::type_index(typeid(PromiseT)) != m_typeIdx) {
            throw std::bad_cast();
        }
#endif
        std::lock_guard lock(m_mtx);
        if (!m_rawHandle)
            return nullptr;
        return std::coroutine_handle<PromiseT>::from_address(m_rawHandle.address());
    }

    bool IsDoneInternal() const noexcept {
        std::lock_guard lock(m_mtx);
        return m_rawHandle && m_rawHandle.done();
    }

    /**
     * @brief Only mark the coroutine started, does not execute it
     *
     * @return true success
     * @return false failure
     */
    bool TryClaimStartInternal() {
        std::lock_guard lock(m_mtx);
        CoroutineState expected = CoroutineState::Created;
        return m_state.compare_exchange_strong(expected, CoroutineState::Started,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed);
    }

    /**
     * @brief mark the coroutine started, and start it once
     *
     * @return true success
     * @return false failure
     */
    bool TryExecuteStartInternal() {
        std::coroutine_handle<> h;
        {
            std::lock_guard lock(m_mtx);
            CoroutineState expected = CoroutineState::Created;
            if (!m_rawHandle || !m_state.compare_exchange_strong(expected, CoroutineState::Started,
                                                                 std::memory_order_acq_rel,
                                                                 std::memory_order_relaxed))
                return false;
            h = m_rawHandle;
        }
        h.resume();
        return true;
    }

  private:
    /**
     * @brief Destroys only unstarted coroutine. In other case - do not destroy
     *
     * @param only_if_unstarted specify if coroutine has been started once or not
     * @return true
     * @return false
     */
    bool DestroyFrameInternal(bool only_if_unstarted) {
        std::coroutine_handle<> raw;
        {
            std::lock_guard lock(m_mtx);
            const CoroutineState current = m_state.load(std::memory_order_relaxed);
            if (current == CoroutineState::Destroyed)
                return false;
            if (only_if_unstarted && current != CoroutineState::Created)
                return false;

            m_state.store(CoroutineState::Destroyed, std::memory_order_release);
            raw = std::exchange(m_rawHandle, nullptr);
        }
        if (raw) {
            raw.destroy();
        }
        return true;
    }
};

/**
 * @brief Class, which stores a shared_ptr to a control block of coroutine.
 * Required to store the shared_ptr to maintain the lifetime over event loop,
 * e.g. Task is finished or cancelled, but the coroutine handle remains in loop.
 * Object would still be valid to work with.
 *
 * Note: the promise usually stores a copy of this handle, so the frame itself
 * keeps the block alive. The shared_ptr count therefore can NOT be used to
 * detect "nobody owns this frame any more" - the owner (Task, task group,
 * scheduler failure path) must call Destroy() explicitly.
 */
class SafeCoroutineHandle {
    std::shared_ptr<CoroutineControlBlock> m_control{nullptr};

  public:
    SafeCoroutineHandle() = default;

    /**
     * @param handle coroutine handle
     * @param idx is a type id of the promise type, so i can return the templated
     * version of coroutine_handle<PromiseT>. Prevents bad cast errors
     * @param stop the stop_source of the promise's cancellation node (pass
     * promise.cancel.GetStopSource()). Stop sources share state, so the handle
     * can request/observe cancellation after the frame is gone. If omitted, the
     * handle gets its own fresh source that is NOT linked to anything.
     */
    explicit SafeCoroutineHandle(std::coroutine_handle<> handle, std::type_index idx,
                                 std::stop_source stop = std::stop_source{})
        : m_control(std::make_shared<CoroutineControlBlock>(handle, idx, std::move(stop))) {}

    SafeCoroutineHandle(const SafeCoroutineHandle&) = default;
    SafeCoroutineHandle& operator=(const SafeCoroutineHandle&) = default;

    SafeCoroutineHandle(SafeCoroutineHandle&&) noexcept = default;
    SafeCoroutineHandle& operator=(SafeCoroutineHandle&&) noexcept = default;

    ~SafeCoroutineHandle() = default;

    void SetState(CoroutineState state) const {
        if (m_control) {
            m_control->SetStateInternal(state);
        }
    }

    template <std::memory_order order = std::memory_order_acquire>
    [[nodiscard]] CoroutineState LoadState() const noexcept {
        if (!m_control) {
            return CoroutineState::Destroyed;
        }
        return m_control->LoadStateInternal<order>();
    }

    void SetControl(std::shared_ptr<CoroutineControlBlock> ctrl) { m_control = std::move(ctrl); }

    /**
     * @brief True while the control block exists and the frame has not been destroyed.
     * A coroutine parked at final_suspend is still valid - it is destroyed by its
     * owner later.
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return m_control && LoadState() != CoroutineState::Destroyed;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }

    /**
     * @brief Marks the coroutine as started. TryResume() does it automatically; call it
     * yourself wherever a coroutine is started WITHOUT going through TryResume():
     * symmetric transfer (Task::await_suspend returning the child's handle) and
     * eager start policies. Without it TryDestroyUnstarted() could destroy a
     * coroutine that already ran.
     * @return false if the frame is already destroyed
     */
    [[nodiscard]]
    bool MarkStarted() const {
        if (!m_control)
            return false;
        return m_control->MarkStartedInternal();
    }

    /**
     * @brief Resumes the coroutine unless it is destroyed or sitting at final_suspend.
     * Does NOT prevent a concurrent resume: the awaiter protocol must guarantee
     * that exactly one party resumes.
     * @return true - the coroutine was resumed
     * @return false - nothing to resume
     */
    [[nodiscard]]
    bool TryResume() const {
        if (!m_control)
            return false;
        return m_control->TryExecuteResumeInternal();
    }

    /**
     * @brief Try to destroy the coroutine, which has not been executed yet
     * @return true - success destroyed the unstarted coroutine, false - coroutine
     * was previously started once (or is already destroyed)
     */
    [[nodiscard]]
    bool TryDestroyUnstarted() const {
        if (!m_control)
            return false;
        return m_control->TryExecuteDestroyUnstartedInternal();
    }

    /**
     * @brief THE destroy path. Owners (Task destructor, scheduler drain,
     * scheduling-failure path) should call this instead of
     * coroutine_handle::destroy(), so the handle is withdrawn from the control
     * block before the frame is torn down and a double destroy is impossible.
     * Precondition: the coroutine is not executing on another thread (request
     * stop and wait for completion first).
     * @return true - this call destroyed the frame,
     * @return false - already destroyed / empty handle
     */
    [[nodiscard("status of destruction of coroutine shouldn't be ignored")]] bool Destroy() const {
        if (!m_control)
            return false;
        return m_control->TryExecuteDestroyInternal();
    }

    /**
     * @tparam PromiseT the type of coroutine_handle<PromiseT> we want to get
     * @return null handle if the frame is destroyed; debug builds throw
     * std::bad_cast on a promise type mismatch. The returned handle is an escape
     * hatch: it is not protected against a later Destroy().
     */
    template <typename PromiseT> std::coroutine_handle<PromiseT> RawHandle() const {
        if (!m_control)
            return nullptr;
        return m_control->GetRawHandleInternal<PromiseT>();
    }

    void RequestStop() const noexcept {
        if (!m_control)
            return;
        (void)m_control->m_stopSource.request_stop();
    }

    [[nodiscard]] std::stop_token GetStopToken() const noexcept {
        if (!m_control)
            return {};
        return m_control->m_stopSource.get_token();
    }

    [[nodiscard]] bool IsCancelled() const noexcept {
        if (!m_control)
            return true;
        return m_control->m_stopSource.stop_requested();
    }

    /**
     * @brief check if coroutine_handle().done or not, does not check for cancellation
     */
    [[nodiscard]] bool IsDone() const noexcept {
        if (!m_control)
            return false;
        return m_control->IsDoneInternal();
    }

    /**
     * @return a copy of the stop source (shares state with the coroutine's
     * cancellation node), e.g. for root tasks
     */
    [[nodiscard]] std::stop_source GetStopSource() const noexcept {
        if (!m_control)
            return std::stop_source{std::nostopstate};
        return m_control->m_stopSource;
    }

    /**
     * @brief Marks the coroutine as started
     */
    [[nodiscard("Status of firstly status coroutine")]]
    bool TryClaimStart() const {
        return m_control && m_control->TryClaimStartInternal();
    }

    /**
     * @returns true if coroutine is only now started
     */
    [[nodiscard]]
    bool TryStart() const {
        return m_control && m_control->TryExecuteStartInternal();
    }

    /**
     * @brief returns an internal block holding a state of coroutine
     * @return a coroutine control block
     */
    [[nodiscard("control block should not be discard")]]
    std::shared_ptr<CoroutineControlBlock> GetControlBlock() const noexcept {
        return m_control;
    }
};
} // namespace async_core::common
