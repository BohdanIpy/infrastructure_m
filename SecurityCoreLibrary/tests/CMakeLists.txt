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

    add_executable(bio_input_stream_test ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp)

    target_link_libraries(bio_input_stream_test
            PRIVATE
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
            GTest::gmock_main
            OpenSSL::SSL
            OpenSSL::Crypto
            SecurityCoreLibrary
    )
    add_test(NAME bio_input_stream_test COMMAND $<TARGET_FILE:bio_input_stream_test>)



endif ()