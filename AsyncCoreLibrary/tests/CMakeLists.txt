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
            "coro_exec_a_method|async_methods.cpp|CoroMethodsTest"
    )

    foreach(test_spec IN LISTS TEST_SUITES)
        string(REPLACE "|" ";" test_tuple "${test_spec}")
        list(GET test_tuple 0 base_target_name)
        list(GET test_tuple 1 source_file)
        list(GET test_tuple 2 base_ctest_name)

        # Loop over both sanitizer variants
        foreach(variant "tsan" "asan" "plain")
            set(target_name "${base_target_name}_${variant}")
            set(ctest_name "${base_ctest_name}_${variant}")

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

            # Apply specific sanitizer flags per target variant
            if(variant STREQUAL "tsan")
                target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
                target_link_options(${target_name} PRIVATE -fsanitize=thread)
            elseif(variant STREQUAL "asan")
                target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
            endif()

            # Register with CTest
            add_test(NAME ${ctest_name} COMMAND $<TARGET_FILE:${target_name}>)
        endforeach()
    endforeach()

endif()