add_library(SecurityCoreLibrary)

find_package(OpenSSL REQUIRED)

target_sources(SecurityCoreLibrary
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            secure_types/crypto_allocator.cppm
            secure_types/core_common_types.cppm

            bio/stream_bio.cppm
            bio/core_bio.cppm

            utility/key_parser.cppm

            security_core_library.cppm
)

target_link_libraries(SecurityCoreLibrary
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)