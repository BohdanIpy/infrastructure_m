if(BUILD_TESTING)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare( googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG main
            GIT_SHALLOW TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(socket_address_test ${CMAKE_CURRENT_SOURCE_DIR}/socket_address_test.cpp)

    target_link_libraries(socket_address_test
        PRIVATE
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
            GTest::gmock_main
            NetworkCoreLibrary
    )

    add_test(NAME socketAddressTest COMMAND $<TARGET_FILE:socket_address_test>)

endif ()