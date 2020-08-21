project(libev CXX C)

file(GLOB_RECURSE
    libev_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

list(FILTER libev_srcs EXCLUDE REGEX "ev_.*\\.c")

include_directories(.)

add_library(libev STATIC ${libev_srcs})
target_compile_options(libev PUBLIC -Wno-error -Wno-unused-value -Wno-comment -Wno-parentheses -Wno-return-type)

set_target_properties(libev PROPERTIES LINKER_LANGUAGE C)
set_target_properties(libev PROPERTIES POSITION_INDEPENDENT_CODE ON)