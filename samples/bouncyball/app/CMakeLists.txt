cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS" )

# ============== Games SDK

# This will include and build swappy from the gamesdk
#add_subdirectory(../../../src/swappy ./swappy-output)   # Swappy Library
#include_directories( "../../../include" ) # Games SDK Public Includes

# This will use swappy from a packaged version
set(GAMESDK_SDK_VERSION "28") # Latest version from prebuilts
include("../../gamesdk.cmake")
add_gamesdk_target()

include_directories( ../../common/include ) # Samples Includes

# ============== Bouncy Ball

include_directories( src/main/cpp )

add_library( native-lib

             SHARED

             src/main/cpp/Circle.cpp
             src/main/cpp/Orbit.cpp
             src/main/cpp/Renderer.cpp
             src/main/cpp/Settings.cpp
             ../../../samples/common/src/Thread.cpp
        )

target_link_libraries( native-lib

                       android
                       EGL
                       GLESv2
                       log
                       gamesdk
                       )
