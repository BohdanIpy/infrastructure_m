add_library(TheoryCore)

target_sources(TheoryCore
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/core_theory.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/state_machine.cppm
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)