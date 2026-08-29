
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
            coro_loop/coro_thread.cppm

            io/io_loop.cppm
            io/io_types.cppm

            io/async_methods/async_accept.cppm
            io/async_methods/async_close.cppm
            io/async_methods/async_read.cppm
            io/async_methods/async_write.cppm
            io/async_methods/async_poll.cppm
            io/async_methods/async_connect.cppm
            io/async_methods/async_shutdown.cppm
            io/async_methods/async_read_socket.cppm
            io/async_methods/async_write_socket.cppm
            io/async_methods/async_read_socket_from.cppm
            io/async_methods/async_write_socket_to.cppm

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