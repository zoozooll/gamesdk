cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Werror -Wthread-safety -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS" )

add_subdirectory(../../swappy ./swappy-output)

add_library( native-lib

             SHARED

             src/main/cpp/Circle.cpp
             src/main/cpp/Orbit.cpp
             src/main/cpp/Renderer.cpp
        )

include_directories( src/main/cpp )

target_link_libraries( native-lib

                       android
                       EGL
                       GLESv2
                       log
                       swappy
                       )