cmake_minimum_required(VERSION 3.4.1)

set( PROTOBUF_VARIANT "ALL")
set( PROTOBUF_SRC_DIR "~/3rdParty/protobuf-3.0.2/src")
set( PROTOBUF_INSTALL_DIR "/usr/local")

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI -DHAVE_PTHREAD")

include_directories(${PROTOBUF_SRC_DIR})
include_directories(${PROTOBUF_NANO_SRC_DIR})

add_subdirectory(../../../../src/tuningfork ./tuningfork_output)

protobuf_generate_nano_c( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_extensions.proto)
protobuf_generate_full_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_extensions.proto)
include_directories(${PROTO_GENS_DIR})
include_directories(../../../../src)
include_directories(../../../../src/tuningfork/gens)
include_directories(../../../../src/tuningfork/gens/full)

add_library( tuningfork_test
             SHARED
             src/test/cpp/nativetests.cpp
             src/test/cpp/histogram_test.cpp
             src/test/cpp/tuningfork_test.cpp
             ${PROTO_GENS_DIR}/full/tuningfork_extensions.pb.cc
        ../../../../src/tuningfork/gens/full/tuningfork.pb.cc
        ../../../../src/tuningfork/gens/full/tuningfork_clearcut_log.pb.cc)

# Note that we use the FULL version of protobuf in the tests, so we can get debug strings
target_link_libraries( tuningfork_test
        tuningfork-static
        protobuf-static
        log
)
set_target_properties( tuningfork_test PROPERTIES COMPILE_OPTIONS "-DPROTOBUF_FULL" )

add_library( native-lib
             SHARED
             src/main/cpp/tftestapp.cpp)

target_link_libraries( native-lib
		       tuningfork_test)
