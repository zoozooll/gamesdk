cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Werror -Wthread-safety -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -O -fPIC" )

include_directories(.)

add_library( swappy

             SHARED

             swappy/ChoreographerFilter.cpp
             swappy/EGL.cpp
             swappy/Swappy.cpp
             swappy-utils/Settings.cpp
             swappy-utils/Thread.cpp )

 target_link_libraries( swappy

                        EGL
                        log )
