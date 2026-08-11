add_library(NetworkCoreLibrary)

target_sources(NetworkCoreLibrary
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/core_api.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/network_core_library.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/socket_address.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/socket_endpoint.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/network_adapters.cppm
)

target_link_libraries(NetworkCoreLibrary
    PUBLIC
        CoreUtility
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)