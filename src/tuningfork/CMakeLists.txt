cmake_minimum_required(VERSION 3.4.1)

# PROTOBUF_VARIANT must be set to FULL, LITE or BOTH
# The LITE version uses the *_lite.proto files with
# option optimize_for = LITE_RUNTIME;
if( NOT DEFINED PROTOBUF_VARIANT)
  message(FATAL_ERROR "PROTOBUF_VARIANT not defined")
endif()
if( NOT DEFINED PROTOBUF_SRC_DIR)
  message(FATAL_ERROR "PROTOBUF_SRC_DIR not defined")
endif()
if( NOT DEFINED PROTOBUF_INSTALL_DIR)
  message(FATAL_ERROR "PROTOBUF_INSTALL_DIR not defined")
endif()

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI -DHAVE_PTHREAD")
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections" )
#The following are needed for some of the protobuf code to compile
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-tautological-constant-compare -Wno-enum-compare-switch")

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections,-s")
endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

function(set_link_options libname versionscript)
  if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  set_target_properties( ${libname} PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/${versionscript}")
  endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")
endfunction()

function(extra_tf_link_options libname)
  set_link_options(${libname} version.script)
endfunction()
function(extra_pb_link_options libname)
  set_link_options(${libname} protobuf_version.script)
endfunction()


include_directories(${PROTOBUF_SRC_DIR})
include_directories( ../../include )
include_directories( ../../src/swappy )
include_directories( ../)

set(_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
set(PROTO_GENS_DIR ${_PROTO_GENS_DIR} PARENT_SCOPE)
file(MAKE_DIRECTORY ${_PROTO_GENS_DIR})
set(_PROTOC_EXECUTABLE ${PROTOBUF_INSTALL_DIR}/bin/protoc)

function(protobuf_generate_cpp)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
    return()
  endif()

  set(_protobuf_include_path -I .)
  set(_working_dir ${ARGV0})
  set(_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
  set(_PROTOC_EXECUTABLE ${PROTOBUF_INSTALL_DIR}/bin/protoc)
  message(STATUS "_working_dir=${_working_dir}")
  message(STATUS "_PROTO_GENS_DIR=${_PROTO_GENS_DIR}")
  message(STATUS "_PROTOC_EXECUTABLE=${_PROTOC_EXECUTABLE}")
  list(REMOVE_AT ARGN 0)
  foreach(FIL ${ARGN})
    message(STATUS "FIL=${FIL}")
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    message(STATUS "ABS_FIL=${ABS_FIL}")
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    message(STATUS "FIL_WE=${FIL_WE}")
    file(RELATIVE_PATH REL_FIL ${_working_dir} ${ABS_FIL})
    message(STATUS "REL_FIL=${REL_FIL}")
    get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
    message(STATUS "REL_DIR=${REL_DIR}")
    set(RELFIL_WE "${REL_DIR}/${FIL_WE}")
    message(STATUS "RELFIL_WE=${RELFIL_WE}")

    add_custom_command(
      OUTPUT "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.cc"
             "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.h"
      COMMAND ${_PROTOC_EXECUTABLE}
      ARGS --cpp_out=${_PROTO_GENS_DIR}
           ${_protobuf_include_path}
           ${REL_FIL}
      DEPENDS ${ABS_FIL} ${_PROTOC_EXECUTABLE}
      WORKING_DIRECTORY ${_working_dir}
      COMMENT "Running C++ protocol buffer compiler on ${FIL}"
      VERBATIM)

      message(STATUS "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.*")

    set_source_files_properties("${_PROTO_GENS_DIR}${RELFIL_WE}.pb.cc" "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.h" PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()

include_directories(${_PROTO_GENS_DIR})

set(PROTOBUF_LITE_SRCS
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

set(PROTOBUF_SRCS

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
add_library( protobuf-lite-static
        STATIC
${PROTOBUF_LITE_SRCS})

add_library( protobuf-lite
SHARED 
${PROTOBUF_LITE_SRCS})
extra_pb_link_options(protobuf-lite)

if (${PROTOBUF_VARIANT} STREQUAL "FULL" OR ${PROTOBUF_VARIANT} STREQUAL "BOTH")
add_library( protobuf-static
        STATIC ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
)

add_library( protobuf
        SHARED ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
)

extra_pb_link_options(protobuf)

endif ()

protobuf_generate_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/proto proto/tuningfork.proto
                       proto/tuningfork_clearcut_log.proto )
protobuf_generate_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/proto proto/tuningfork_lite.proto
                       proto/tuningfork_clearcut_log_lite.proto )

set( TUNINGFORK_SRCS
            histogram.cpp
            prong.cpp
            uploadthread.cpp
            tuningfork.cpp
	    tuningfork_c.cpp )
set( TF_PROTO_SRCS
            ${_PROTO_GENS_DIR}/tuningfork.pb.cc
            ${_PROTO_GENS_DIR}/tuningfork_clearcut_log.pb.cc)
set( TF_PROTO_LITE_SRCS
            ${_PROTO_GENS_DIR}/tuningfork_lite.pb.cc
            ${_PROTO_GENS_DIR}/tuningfork_clearcut_log_lite.pb.cc)

add_library( tuningfork-pl-static
            STATIC
${TUNINGFORK_SRCS})

set_target_properties( tuningfork-pl-static PROPERTIES
     COMPILE_OPTIONS "-DPROTOBUF_LITE" )

if (${PROTOBUF_VARIANT} STREQUAL "LITE" OR ${PROTOBUF_VARIANT} STREQUAL "BOTH")

# Proto-lite, shared linkage
add_library( tuningfork-pl
	     SHARED ${TUNINGFORK_SRCS}	${TF_PROTO_LITE_SRCS})
set_target_properties( tuningfork-pl PROPERTIES
     COMPILE_OPTIONS "-DPROTOBUF_LITE" )
target_link_libraries( tuningfork-pl
                      log
                      protobuf-lite)
extra_tf_link_options( tuningfork-pl )

# Proto-lite, statically linked
add_library( tuningfork-pls
	     SHARED ${TUNINGFORK_SRCS} ${TF_PROTO_LITE_SRCS})
set_target_properties( tuningfork-pls PROPERTIES
     COMPILE_OPTIONS "-DPROTOBUF_LITE" )
target_link_libraries( tuningfork-pls
                      log
                      protobuf-lite-static)
extra_tf_link_options( tuningfork-pls )
endif()

if (${PROTOBUF_VARIANT} STREQUAL "FULL" OR ${PROTOBUF_VARIANT} STREQUAL "BOTH")

add_library( tuningfork-p-static
	     STATIC ${TUNINGFORK_SRCS} ${TF_PROTO_SRCS})
set_target_properties( tuningfork-p-static PROPERTIES
     COMPILE_OPTIONS "-DPROTOBUF_FULL" )

# Proto full, shared linkage
add_library( tuningfork-p
	     SHARED ${TUNINGFORK_SRCS} ${TF_PROTO_SRCS})
set_target_properties( tuningfork-p PROPERTIES
     COMPILE_OPTIONS "-DPROTOBUF_FULL" )
target_link_libraries( tuningfork-p
                      log
                      protobuf)
extra_tf_link_options( tuningfork-p )

#Proto full, statically linked
add_library( tuningfork-ps
	     SHARED ${TUNINGFORK_SRCS} ${TF_PROTO_SRCS})
set_target_properties( tuningfork-ps PROPERTIES
     COMPILE_OPTIONS "-DPROTOBUF_FULL" )
target_link_libraries( tuningfork-ps
                      log
                      protobuf-static)
extra_tf_link_options( tuningfork-ps )

endif ()

