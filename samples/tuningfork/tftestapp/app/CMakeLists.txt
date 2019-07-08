cmake_minimum_required(VERSION 3.4.1)

# If you have protobuf installed from a different directory, set it here. The source version
#  must match the protoc version.
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI -DHAVE_PTHREAD")

set( GAMESDK_BUILD_TUNINGFORK ON)
include("../../../gamesdk.cmake")

# This builds the gamesdk in package/localtf and adds a gamesdk static library target
add_gamesdk_target()
# This uses a pre-built gamesdk, unpacked in package/gamesdk
#add_gamesdk_target("package/gamesdk" "" OFF "r17" "28")

# Uncomment to add the Game SDK sources as part of the project sources, allowing to develop
# (with auto completions) and debug Tuning Fork from Android Studio using this sample.
#add_gamesdk_sources()

include("../../../../src/protobuf/protobuf.cmake")

protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/dev_tuningfork.proto)
protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork.proto)
protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_clearcut_log.proto)
include_directories(${PROTO_GENS_DIR})
include_directories(${PROTOBUF_SRC_DIR})
include_directories(${PROTOBUF_NANO_SRC_DIR})
include_directories(../../../../src)
include_directories(../../../common/include)
include_directories(../../../../include)

# Note that we use the FULL version of protobuf in the testapp, so we can get debug strings
add_library( protobuf-static
             STATIC
             ${PROTOBUF_LITE_SRCS}
             ${PROTOBUF_SRCS}
)
target_compile_options(protobuf-static PUBLIC "-Wno-tautological-constant-compare" "-Wno-enum-compare-switch")

add_library( native-lib
             SHARED
             src/main/cpp/tftestapp.cpp
             src/main/cpp/Renderer.cpp
             src/main/cpp/Scene.cpp
             ${PROTO_GENS_DIR}/full/dev_tuningfork.pb.cc
             ${PROTO_GENS_DIR}/full/tuningfork.pb.cc
             ${PROTO_GENS_DIR}/full/tuningfork_clearcut_log.pb.cc
)

target_link_libraries( native-lib
        gamesdk
        protobuf-static
        android
        EGL
        GLESv2
        log
)
