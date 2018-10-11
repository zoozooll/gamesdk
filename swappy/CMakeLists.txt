cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Werror -Wthread-safety -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -O -fPIC" )

add_library( swappy

             SHARED

             ChoreographerFilter.cpp
             EGL.cpp
             Swappy.cpp
             Settings.cpp
             Thread.cpp )

 target_link_libraries( swappy

                        android
                        EGL
                        GLESv2
                        log )
