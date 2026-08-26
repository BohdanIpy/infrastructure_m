if(BUILD_TESTING)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG main
            GIT_SHALLOW TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    set(TEST_SUITES
            "coro_exec|exec_test.cpp|CoroExecTest"
            "coro_exec_time|time_test.cpp|CoroTimeTest"
            "coro_exec_sync|sync_test.cpp|CoroSyncTest"
            "coro_exec_channel|channel_test.cpp|CoroChannelTest"
            "coro_exec_io|io_test.cpp|CoroIoTest"
            "coro_exec_context|context_test.cpp|CoroContextTest"
    )

    foreach(test_spec IN LISTS TEST_SUITES)

        string(REPLACE "|" ";" test_tuple "${test_spec}")
        list(GET test_tuple 0 target_name)
        list(GET test_tuple 1 source_file)
        list(GET test_tuple 2 ctest_name)

        # Build Executable Target
        add_executable(${target_name} ${CMAKE_CURRENT_SOURCE_DIR}/${source_file})

        target_link_libraries(${target_name}
                PRIVATE
                GTest::gtest
                GTest::gtest_main
                GTest::gmock
                GTest::gmock_main
                AsyncCoreLibrary
        )

        # ThreadSanitizer Flags
        target_compile_options(${target_name} PRIVATE -fsanitize=thread)
        target_link_options(${target_name} PRIVATE -fsanitize=thread)

        # Register with CTest
        add_test(NAME ${ctest_name} COMMAND $<TARGET_FILE:${target_name}>)
    endforeach()

endif()