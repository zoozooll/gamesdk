cmake_minimum_required(VERSION 3.4.1)

set( PROTOBUF_VARIANT "FULL")
set( PROTOBUF_SRC_DIR "~/3rdParty/protobuf-3.0.2/src")
set( PROTOBUF_INSTALL_DIR "/usr/local")

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI")

include_directories(${PROTOBUF_SRC_DIR})

add_subdirectory(../../../../src/tuningfork ./tuningfork_output)

protobuf_generate_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork_extensions.proto)
include_directories(${PROTO_GENS_DIR})

include_directories(../../../../src)

add_library( tuningfork_test
             SHARED
             src/test/cpp/nativetests.cpp
             src/test/cpp/histogram_test.cpp
             src/test/cpp/tuningfork_test.cpp
             ${PROTO_GENS_DIR}/tuningfork_extensions.pb.cc)

target_link_libraries( tuningfork_test
                       tuningfork-p-static
                       protobuf-static
                       log)

add_library( native-lib
             SHARED
             src/main/cpp/tftest.cpp
)

target_link_libraries( native-lib
		       tuningfork_test)
