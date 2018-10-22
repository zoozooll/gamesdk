cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -std=c++17 -Werror -Wthread-safety -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )

include_directories( ../../includes )

message( STATUS "Building swappy_static to ${CMAKE_CURRENT_BINARY_DIR}/build" )
add_library( swappy_static

             STATIC

             ChoreographerFilter.cpp
             EGL.cpp
             Swappy.cpp
             Settings.cpp
             Thread.cpp
             # Add new source files here 
             )

set_target_properties( swappy_static PROPERTIES
     LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build )

add_library( swappy

             SHARED

             Swappy.cpp )   # Looks like I need to specify at least one file to build,
                            # the result .so size suggests it's including the rest of the
                            # source files from the library
                            # also, removing the target_link to the static library
                            # fails the build


 target_link_libraries( swappy

                        swappy_static
                        android
                        EGL
                        GLESv2
                        log )
