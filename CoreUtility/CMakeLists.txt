add_library(CoreUtility)

target_sources(CoreUtility
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            core_utility.cppm

            errors/io_error.cppm

            helpers/wrappers.cppm
            helpers/time.cppm
)