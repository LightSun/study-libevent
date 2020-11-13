cmake_minimum_required(VERSION 3.4.1)

aux_source_directory(src/main/cpp SRC_libevent)

add_library(
        libevent
        SHARED
        ${SRC_libevent}
)

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)


target_link_libraries(
        libevent
        ${log-lib}
)