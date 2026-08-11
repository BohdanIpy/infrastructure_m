cmake_minimum_required(VERSION 3.28)
project(NetworkSocketCpp CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/CoreUtility)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/BasicSocket)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/NetworkCoreLibrary)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SecurityCoreLibrary)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/CoreTheory)

find_package(OpenSSL REQUIRED)

set(executables "client" "server" "time_server")

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
