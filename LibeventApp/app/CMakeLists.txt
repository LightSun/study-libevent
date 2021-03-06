cmake_minimum_required(VERSION 3.4.1)

include_directories(src/main/cpp)
aux_source_directory(src/main/cpp SRC_libevent)
aux_source_directory(src/main/cpp/libio SRC_libio)

add_library(
        libevent
        STATIC
        ${SRC_libevent}
)

add_library(
        jnitest
        SHARED
        ${SRC_libio}
        src/main/cpp/sample/event-test.c
        src/main/cpp/sample/main_jni.cpp
        src/main/cpp/sample/bufevent_server_demo.cpp
        src/main/cpp/sample/bufevent_client_demo.cpp
        src/main/cpp/sample/demo.cpp
        src/main/cpp/sample/libio_demo.cpp
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