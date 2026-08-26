cmake_minimum_required(VERSION 3.0...3.28)
project(NetworkSocketCpp CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/CoreUtility)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/NetworkCoreLibrary)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SecurityCoreLibrary)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/CoreTheory)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/AsyncCoreLibrary)

find_package(OpenSSL REQUIRED)

set(executables "client" "server" "time_server" "basic_setup" "basic_resolve" "ares")

foreach (ITEM IN LISTS executables)
    add_executable(${ITEM} src/${ITEM}/main.cpp)
    target_link_libraries(${ITEM}
        PRIVATE
            CoreUtility
            SecurityCoreLibrary
            NetworkCoreLibrary
            OpenSSL::SSL
            OpenSSL::Crypto
    )
endforeach ()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")

# for ares

include(FetchContent)

FetchContent_Declare(
        c-ares
        GIT_REPOSITORY https://github.com/c-ares/c-ares.git
        GIT_TAG        v1.30.0
)

set(CARES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(CARES_STATIC ON CACHE BOOL "" FORCE)
set(CARES_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(c-ares)

target_link_libraries(ares
    PRIVATE
        c-ares::cares
)
