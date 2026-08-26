
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

add_library(NetworkCoreLibrary)

target_sources(NetworkCoreLibrary
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            core_api.cppm
            network_core_library.cppm
            socket_address.cppm
            socket_endpoint.cppm
            network_adapters.cppm
            basic_socket.cppm

            resolving/resolution_context.cppm
            resolving/dns_resolver.cppm
)

target_link_libraries(NetworkCoreLibrary
    PUBLIC
        AsyncCoreLibrary
        CoreUtility
        c-ares::cares
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)