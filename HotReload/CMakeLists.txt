add_library(HotReload STATIC)

target_sources(HotReload
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES
                hot_reload.cppm
)
