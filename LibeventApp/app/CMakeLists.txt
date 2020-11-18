cmake_minimum_required(VERSION 3.4.1)

include_directories(src/main/cpp)
aux_source_directory(src/main/cpp SRC_libevent)

add_library(
        libevent
        STATIC
        ${SRC_libevent}
)

add_library(
        jnitest
        SHARED
        src/main/cpp/sample/event-test.c
        src/main/cpp/sample/main_jni.cpp
)

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

target_link_libraries(
        jnitest
        libevent
        ${log-lib}
)