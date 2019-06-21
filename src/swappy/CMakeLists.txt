cmake_minimum_required(VERSION 3.4.1)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -O3 -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections" )
if ( DEFINED GAMESDK_THREAD_CHECKS )
  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGAMESDK_THREAD_CHECKS=${GAMESDK_THREAD_CHECKS}" )
endif()

set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections" )
set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-s" )

set ( SOURCE_LOCATION .)
set ( SOURCE_LOCATION_COMMON "${SOURCE_LOCATION}/common" )
set ( SOURCE_LOCATION_OPENGL "${SOURCE_LOCATION}/opengl" )
set ( SOURCE_LOCATION_VULKAN "${SOURCE_LOCATION}/vulkan" )

include_directories( ../../include )
include_directories( ../common )
include_directories( common )
include_directories( opengl )
include_directories( vulkan )
include_directories( ${ANDROID_NDK}/sources/third_party/vulkan/src/common )

message( STATUS "Building swappy_static to ${CMAKE_CURRENT_BINARY_DIR}/build" )
add_library( swappy_static

             STATIC

             ${SOURCE_LOCATION_COMMON}/ChoreographerFilter.cpp
             ${SOURCE_LOCATION_COMMON}/ChoreographerThread.cpp
             ${SOURCE_LOCATION_COMMON}/FrameStatistics.cpp
             ${SOURCE_LOCATION_COMMON}/CpuInfo.cpp
             ${SOURCE_LOCATION_COMMON}/SystemProperties.cpp
             ${SOURCE_LOCATION_COMMON}/Settings.cpp
             ${SOURCE_LOCATION_COMMON}/Thread.cpp
             ${SOURCE_LOCATION_COMMON}/SwappyCommon.cpp
             ${SOURCE_LOCATION_COMMON}/swappy_c.cpp
             ${SOURCE_LOCATION_COMMON}/SwappyDisplayManager.cpp
             ${SOURCE_LOCATION_COMMON}/CPUTracer.cpp
             ${SOURCE_LOCATION_OPENGL}/EGL.cpp
             ${SOURCE_LOCATION_OPENGL}/swappyGL_c.cpp
             ${SOURCE_LOCATION_OPENGL}/SwappyGL.cpp
             ${ANDROID_NDK}/sources/third_party/vulkan/src/common/vulkan_wrapper.cpp
             ${SOURCE_LOCATION_VULKAN}/swappyVk_c.cpp
             ${SOURCE_LOCATION_VULKAN}/SwappyVk.cpp
             ${SOURCE_LOCATION_VULKAN}/SwappyVkBase.cpp
             ${SOURCE_LOCATION_VULKAN}/SwappyVkFallback.cpp
             ${SOURCE_LOCATION_VULKAN}/SwappyVkGoogleDisplayTiming.cpp

             # Add new source files here
             )

set_target_properties( swappy_static PROPERTIES
     LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build )

add_library( swappy

             SHARED

             ${SOURCE_LOCATION_COMMON}/swappy_c.cpp
             ${SOURCE_LOCATION_OPENGL}/swappyGL_c.cpp
             ${SOURCE_LOCATION_VULKAN}/swappyVk_c.cpp)


 target_link_libraries( swappy

                        swappy_static
                        android
                        EGL
                        GLESv2
                        log )
