project(libev CXX)

file(GLOB_RECURSE
    libev_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

list(FILTER libev_srcs EXCLUDE REGEX "ev_.*\\.cpp")

include_directories(.)

add_library(libev STATIC ${libev_srcs})

target_compile_options(libev PUBLIC -Wno-error -Wno-unused-value -Wno-comment -Wno-parentheses -Wno-return-type
        -Wno-incompatible-pointer-types -Wno-unused-variable -Wno-unused-function)
set_target_properties(libev PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(libev PROPERTIES POSITION_INDEPENDENT_CODE ON)