cmake_minimum_required(VERSION 3.4.1)

# If you have protobuf installed from a different directory, set it here. The source version
#  must match the protoc version.
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI -DHAVE_PTHREAD")

#set( BUILD_PROTOBUF_FULL_AND_LITE "True")
#add_subdirectory(../../../../src/tuningfork ./tuningfork_output)

# This will use tuningfork from a packaged version
set( GAMESDK_BUILD_TUNINGFORK ON)
set(GAMESDK_SDK_VERSION "28") # Latest version from prebuilts
include("../../../gamesdk.cmake")
add_gamesdk_target()
include("../../../../src/protobuf/protobuf.cmake")

protobuf_generate_nano_c( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_extensions.proto)
protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_extensions.proto)
protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork.proto)
protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_clearcut_log.proto)
include_directories(${PROTO_GENS_DIR})
include_directories(${PROTOBUF_SRC_DIR})
include_directories(${PROTOBUF_NANO_SRC_DIR})
include_directories(../../../../src)

add_library( protobuf-static
        STATIC ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
        )
target_compile_options(protobuf-static PUBLIC "-Wno-tautological-constant-compare" "-Wno-enum-compare-switch")

add_library( tuningfork_test
             SHARED
             src/test/cpp/nativetests.cpp
             src/test/cpp/histogram_test.cpp
             src/test/cpp/tuningfork_test.cpp
             ${PROTO_GENS_DIR}/full/tuningfork_extensions.pb.cc
             ${PROTO_GENS_DIR}/full/tuningfork.pb.cc
             ${PROTO_GENS_DIR}/full/tuningfork_clearcut_log.pb.cc)

# Note that we use the FULL version of protobuf in the tests, so we can get debug strings
target_link_libraries( tuningfork_test
        gamesdk
        protobuf-static
        log
)
set_target_properties( tuningfork_test PROPERTIES COMPILE_OPTIONS "-DPROTOBUF_FULL" )

add_library( native-lib
             SHARED
             src/main/cpp/tftestapp.cpp)

target_link_libraries( native-lib
		       tuningfork_test)
