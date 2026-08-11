add_library(SecurityCoreLibrary)

find_package(OpenSSL REQUIRED)

target_sources(SecurityCoreLibrary
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/core_bio.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/core_common_types.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/security_core_library.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/crypto_allocator.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/stream_bio.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/security_utility.cppm
)

target_link_libraries(SecurityCoreLibrary
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)