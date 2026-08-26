
add_library(AsyncCoreLibrary)

target_sources(AsyncCoreLibrary
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            core_async.cppm

            exec/detached_task.cppm
            exec/generator.cppm
            exec/task.cppm
            exec/safe_coroutine_handle.cppm

            context/switch_to_thread.cppm

            coro_loop/i_coro_scheduler.cppm
            coro_loop/coro_scheduler.cppm
            coro_loop/switch_to.cppm

            errors/io_error.cppm

            io/io_loop.cppm
            io/io_types.cppm
            io/async_methods/async_accept.cppm
            io/async_methods/async_close.cppm
            io/async_methods/async_read.cppm
            io/async_methods/async_write.cppm
            io/async_methods/async_poll.cppm

            sync/async_auto_reset_event.cppm
            sync/async_channel.cppm
            sync/async_manual_reset_event.cppm
            sync/async_mutex.cppm
            sync/async_semaphore.cppm
            sync/sync_wait.cppm
            sync/when_all.cppm

            time/timer_queue.cppm
            time/sleep_for.cppm
            time/sleep_until.cppm
            time/timeout_after.cppm
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)

target_link_libraries(AsyncCoreLibrary
    PRIVATE
        PkgConfig::URING
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)