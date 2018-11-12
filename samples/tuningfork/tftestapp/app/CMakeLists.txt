cmake_minimum_required(VERSION 3.4.1)

 # Use the LITE or FULL version of protobuf
 # Note you'll need to add the following line to the protos if you use LITE:
 # option optimize_for = LITE_RUNTIME;
set( PROTOBUF_VERSION "FULL")
set( PROTOBUF_SRC_DIR "~/3rdParty/protobuf-3.0.2/src" )
set( PROTOBUF_INSTALL_DIR "/usr/local" )


set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI -DHAVE_PTHREAD")
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections" )
#The following are needed for some of the protobuf code to compile
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-tautological-constant-compare -Wno-enum-compare-switch")

if (${PROTOBUF_VERSION} STREQUAL "LITE")
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPROTOBUF_LITE")
elseif (${PROTOBUF_VERSION} STREQUAL "FULL")
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPROTOBUF_FULL")
endif (${PROTOBUF_VERSION} STREQUAL "LITE")

#if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections,-s")
#endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

include_directories(${PROTOBUF_SRC_DIR})

set(_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
file(MAKE_DIRECTORY ${_PROTO_GENS_DIR})
set(_PROTOC_EXECUTABLE ${PROTOBUF_INSTALL_DIR}/bin/protoc)

function(protobuf_generate_cpp)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
    return()
  endif()

  set(_protobuf_include_path -I .)
  foreach(FIL ${ARGN})
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    file(RELATIVE_PATH REL_FIL ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto ${ABS_FIL})
    get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
    set(RELFIL_WE "${REL_DIR}/${FIL_WE}")

    add_custom_command(
      OUTPUT "${_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc"
             "${_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h"
      COMMAND ${_PROTOC_EXECUTABLE}
      ARGS --cpp_out=${_PROTO_GENS_DIR}
           ${_protobuf_include_path}
           ${REL_FIL}
      DEPENDS ${ABS_FIL} ${_PROTOC_EXECUTABLE}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto
      COMMENT "Running C++ protocol buffer compiler on ${FIL}"
      VERBATIM)

    set_source_files_properties("${_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc" "${_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h" PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()

include_directories(${_PROTO_GENS_DIR})

add_library( protobuf-lite

        SHARED

        ${PROTOBUF_SRC_DIR}/google/protobuf/arena.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/arenastring.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/extension_set.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/generated_message_util.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/io/coded_stream.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/io/zero_copy_stream.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/io/zero_copy_stream_impl_lite.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/message_lite.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/repeated_field.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/atomicops_internals_x86_gcc.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/atomicops_internals_x86_msvc.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/bytestream.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/common.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/int128.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/once.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/status.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/statusor.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/stringpiece.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/stringprintf.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/structurally_valid.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/strutil.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/time.cc
        ${PROTOBUF_SRC_DIR}/google/protobuf/wire_format_lite.cc)

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
set_target_properties( protobuf-lite PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/protobuf_version.script,--gc-sections,-s")
endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

if (${PROTOBUF_VERSION} STREQUAL "FULL")
add_library( protobuf

        SHARED

          ${PROTOBUF_SRC_DIR}/google/protobuf/any.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/any.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/api.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/compiler/importer.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/compiler/parser.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/descriptor.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/descriptor.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/descriptor_database.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/duration.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/dynamic_message.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/empty.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/extension_set_heavy.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/field_mask.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/generated_message_reflection.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/io/gzip_stream.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/io/printer.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/io/strtod.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/io/tokenizer.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/io/zero_copy_stream_impl.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/map_field.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/message.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/reflection_ops.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/service.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/source_context.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/struct.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/mathlimits.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/stubs/substitute.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/text_format.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/timestamp.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/type.pb.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/unknown_field_set.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/field_comparator.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/field_mask_util.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/datapiece.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/default_value_objectwriter.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/error_listener.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/field_mask_utility.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/json_escaping.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/json_objectwriter.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/json_stream_parser.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/object_writer.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/proto_writer.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/protostream_objectsource.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/protostream_objectwriter.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/type_info.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/type_info_test_helper.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/internal/utility.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/json_util.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/message_differencer.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/time_util.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/util/type_resolver_util.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/wire_format.cc
          ${PROTOBUF_SRC_DIR}/google/protobuf/wrappers.pb.cc)

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
set_target_properties( protobuf PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/protobuf_version.script,--gc-sections,-s")
endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

target_link_libraries( protobuf
                       protobuf-lite)
endif (${PROTOBUF_VERSION} STREQUAL "FULL")

protobuf_generate_cpp( src/main/proto/tuningfork.proto
                       src/main/proto/tuningfork_clearcut_log.proto )

add_library( tuningfork

            SHARED

            src/main/cpp/histogram.cpp
            src/main/cpp/prong.cpp
            src/main/cpp/uploadthread.cpp
            src/main/cpp/tuningfork.cpp
            ${_PROTO_GENS_DIR}/tuningfork.pb.cc
            ${_PROTO_GENS_DIR}/tuningfork_clearcut_log.pb.cc)

if (${PROTOBUF_VERSION} STREQUAL "LITE")
target_link_libraries( tuningfork
                      log
                      protobuf-lite)
elseif (${PROTOBUF_VERSION} STREQUAL "FULL")
target_link_libraries( tuningfork
                      log
                      protobuf)
else (${PROTOBUF_VERSION} STREQUAL "LITE")
target_link_libraries( tuningfork
                      log)
endif (${PROTOBUF_VERSION} STREQUAL "LITE")

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
set_target_properties( tuningfork PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/version.script,--gc-sections,-s")
endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

protobuf_generate_cpp( src/main/proto/tuningfork_extensions.proto )

add_library( tuningfork_test
             SHARED
             src/test/cpp/histogram_test.cpp
             src/test/cpp/tuningfork_test.cpp
             ${_PROTO_GENS_DIR}/tuningfork_extensions.pb.cc)

target_link_libraries( tuningfork_test
                       tuningfork)

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
set_target_properties( tuningfork_test PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/version.script,--gc-sections,-s")
endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

add_library( native-lib
             SHARED
             src/test/cpp/nativetests.cpp
)

target_link_libraries( native-lib
		       tuningfork_test)
