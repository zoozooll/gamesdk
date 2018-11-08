cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Werror -Wthread-safety -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS" )

# ============== Games SDK

include_directories( ../../../include ) # Games SDK Public Includes
include_directories( ../../../src ) # Games SDK Internal Includes

add_subdirectory(../../../src/swappy ./swappy-output)   # Swappy Library

# ============== Bouncy Ball

include_directories( src/main/cpp )

add_library( native-lib

             SHARED

             src/main/cpp/Circle.cpp
             src/main/cpp/Orbit.cpp
             src/main/cpp/Renderer.cpp
        )


target_link_libraries( native-lib

                       android
                       EGL
                       GLESv2
                       log
                       swappy
                       )