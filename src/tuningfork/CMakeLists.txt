cmake_minimum_required(VERSION 3.4.1)

# Note that you will need to install protobuf from this directory for this default to work,
#  and use the same version of protoc below.
# If autogen.sh complains about gmock, comment out the check for it
if( NOT DEFINED PROTOBUF_SRC_DIR)
  set( PROTOBUF_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/protobuf/src")
endif()
# To use this default, you'll need to run 'protoc-gen-nanopb'
#  in external/nanopb-c/generator
if( NOT DEFINED PROTOBUF_NANO_SRC_DIR)
  set( PROTOBUF_NANO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/nanopb-c")
endif()
if( NOT DEFINED PROTOBUF_INSTALL_DIR)
  set(PROTOC_EXECUTABLE protoc)
else()
  set(PROTOC_EXECUTABLE ${PROTOBUF_INSTALL_DIR}/bin/protoc)
endif()

set( MODPB64_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/modp_b64")

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
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

function(extra_pb_nano_link_options libname)
  set_link_options(${libname} protobuf_nano_version.script)
endfunction()

include_directories(${PROTOBUF_SRC_DIR})
include_directories(${PROTOBUF_NANO_SRC_DIR})
include_directories( ../../include )
include_directories( ../../src/swappy )
include_directories( ../)
include_directories( ${MODPB64_DIR}/modp_b64)

set(_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
set(PROTO_GENS_DIR ${_PROTO_GENS_DIR} PARENT_SCOPE)
set(PROTOC_EXECUTABLE ${PROTOC_EXECUTABLE} PARENT_SCOPE)
set(PROTOBUF_SRC_DIR ${PROTOBUF_SRC_DIR} PARENT_SCOPE)
set(PROTOBUF_NANO_SRC_DIR ${PROTOBUF_NANO_SRC_DIR} PARENT_SCOPE)
file(MAKE_DIRECTORY ${_PROTO_GENS_DIR}/full)
file(MAKE_DIRECTORY ${_PROTO_GENS_DIR}/lite)
file(MAKE_DIRECTORY ${_PROTO_GENS_DIR}/nano)

function(protobuf_generate_base)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
    return()
  endif()

  set(_extension ${ARGV0})
  set(_out_opt ${ARGV1})
  set(_extra_opts ${ARGV2})
  set(_gens_subdir ${ARGV3})
  set(_working_dir ${ARGV4})
  list(REMOVE_AT ARGN 0)
  list(REMOVE_AT ARGN 0)
  list(REMOVE_AT ARGN 0)
  list(REMOVE_AT ARGN 0)
  list(REMOVE_AT ARGN 0)

  set(_protobuf_include_path -I .)
  set(_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens${_gens_subdir})
#  message(STATUS "_working_dir=${_working_dir}")
#  message(STATUS "_PROTO_GENS_DIR=${_PROTO_GENS_DIR}")
#  message(STATUS "_extension=${_extension}")
#  message(STATUS "PROTOC_EXECUTABLE=${PROTOC_EXECUTABLE}")
  foreach(FIL ${ARGN})
#    message(STATUS "FIL=${FIL}")
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
#    message(STATUS "ABS_FIL=${ABS_FIL}")
    get_filename_component(FIL_WE ${FIL} NAME_WE)
#    message(STATUS "FIL_WE=${FIL_WE}")
    file(RELATIVE_PATH REL_FIL ${_working_dir} ${ABS_FIL})
#    message(STATUS "REL_FIL=${REL_FIL}")
    get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
#    message(STATUS "REL_DIR=${REL_DIR}")
    set(RELFIL_WE "${REL_DIR}/${FIL_WE}")
#    message(STATUS "RELFIL_WE=${RELFIL_WE}")

    add_custom_command(
      OUTPUT "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.${_extension}"
             "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.h"
      COMMAND ${PROTOC_EXECUTABLE}
      ARGS ${_out_opt}=${_PROTO_GENS_DIR}
           ${_extra_opts}
           ${_protobuf_include_path}
           ${REL_FIL}
      WORKING_DIRECTORY ${_working_dir}
      COMMENT "Running C++ protocol buffer compiler on ${FIL}"
      VERBATIM)

#      message(STATUS "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.*")

    set_source_files_properties(
      "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.${_extension}"
      "${_PROTO_GENS_DIR}${RELFIL_WE}.pb.h" PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()

get_filename_component(ABS_PROTOBUF_NANO_SRC_DIR ${PROTOBUF_NANO_SRC_DIR} ABSOLUTE)

function(protobuf_generate_nano_c)
  protobuf_generate_base("c" "--nanopb_out"
    "--plugin=protoc-gen-nanopb=${ABS_PROTOBUF_NANO_SRC_DIR}/generator/protoc-gen-nanopb"
    "/nano" ${ARGN})
endfunction()

function(protobuf_generate_full_cpp)
  protobuf_generate_base("cc" "--cpp_out" "" "/full" ${ARGN})
endfunction()

function(protobuf_generate_lite_cpp)
  protobuf_generate_base("cc" "--cpp_out" "" "/lite" ${ARGN})
endfunction()

include_directories(${_PROTO_GENS_DIR})

if(DEFINED BUILD_PROTOBUF_FULL_AND_LITE)
  set(GP_SRC_DIR ${PROTOBUF_SRC_DIR}/google/protobuf)
  set(PROTOBUF_LITE_SRCS
    ${GP_SRC_DIR}/arena.cc
    ${GP_SRC_DIR}/arenastring.cc
    ${GP_SRC_DIR}/extension_set.cc
    ${GP_SRC_DIR}/generated_message_util.cc
    ${GP_SRC_DIR}/io/coded_stream.cc
    ${GP_SRC_DIR}/io/zero_copy_stream.cc
    ${GP_SRC_DIR}/io/zero_copy_stream_impl_lite.cc
    ${GP_SRC_DIR}/message_lite.cc
    ${GP_SRC_DIR}/repeated_field.cc
    ${GP_SRC_DIR}/stubs/atomicops_internals_x86_gcc.cc
    ${GP_SRC_DIR}/stubs/atomicops_internals_x86_msvc.cc
    ${GP_SRC_DIR}/stubs/bytestream.cc
    ${GP_SRC_DIR}/stubs/common.cc
    ${GP_SRC_DIR}/stubs/int128.cc
    ${GP_SRC_DIR}/stubs/once.cc
    ${GP_SRC_DIR}/stubs/status.cc
    ${GP_SRC_DIR}/stubs/statusor.cc
    ${GP_SRC_DIR}/stubs/stringpiece.cc
    ${GP_SRC_DIR}/stubs/stringprintf.cc
    ${GP_SRC_DIR}/stubs/structurally_valid.cc
    ${GP_SRC_DIR}/stubs/strutil.cc
    ${GP_SRC_DIR}/stubs/time.cc
    ${GP_SRC_DIR}/wire_format_lite.cc)
      
  set(PROTOBUF_SRCS
    
    ${GP_SRC_DIR}/any.cc
    ${GP_SRC_DIR}/any.pb.cc
    ${GP_SRC_DIR}/api.pb.cc
    ${GP_SRC_DIR}/compiler/importer.cc
    ${GP_SRC_DIR}/compiler/parser.cc
    ${GP_SRC_DIR}/descriptor.cc
    ${GP_SRC_DIR}/descriptor.pb.cc
    ${GP_SRC_DIR}/descriptor_database.cc
    ${GP_SRC_DIR}/duration.pb.cc
    ${GP_SRC_DIR}/dynamic_message.cc
    ${GP_SRC_DIR}/empty.pb.cc
    ${GP_SRC_DIR}/extension_set_heavy.cc
    ${GP_SRC_DIR}/field_mask.pb.cc
    ${GP_SRC_DIR}/generated_message_reflection.cc
    ${GP_SRC_DIR}/io/gzip_stream.cc
    ${GP_SRC_DIR}/io/printer.cc
    ${GP_SRC_DIR}/io/strtod.cc
    ${GP_SRC_DIR}/io/tokenizer.cc
    ${GP_SRC_DIR}/io/zero_copy_stream_impl.cc
    ${GP_SRC_DIR}/map_field.cc
    ${GP_SRC_DIR}/message.cc
    ${GP_SRC_DIR}/reflection_ops.cc
    ${GP_SRC_DIR}/service.cc
    ${GP_SRC_DIR}/source_context.pb.cc
    ${GP_SRC_DIR}/struct.pb.cc
    ${GP_SRC_DIR}/stubs/mathlimits.cc
    ${GP_SRC_DIR}/stubs/substitute.cc
    ${GP_SRC_DIR}/text_format.cc
    ${GP_SRC_DIR}/timestamp.pb.cc
    ${GP_SRC_DIR}/type.pb.cc
    ${GP_SRC_DIR}/unknown_field_set.cc
    ${GP_SRC_DIR}/util/field_comparator.cc
    ${GP_SRC_DIR}/util/field_mask_util.cc
    ${GP_SRC_DIR}/util/internal/datapiece.cc
    ${GP_SRC_DIR}/util/internal/default_value_objectwriter.cc
    ${GP_SRC_DIR}/util/internal/error_listener.cc
    ${GP_SRC_DIR}/util/internal/field_mask_utility.cc
    ${GP_SRC_DIR}/util/internal/json_escaping.cc
    ${GP_SRC_DIR}/util/internal/json_objectwriter.cc
    ${GP_SRC_DIR}/util/internal/json_stream_parser.cc
    ${GP_SRC_DIR}/util/internal/object_writer.cc
    ${GP_SRC_DIR}/util/internal/proto_writer.cc
    ${GP_SRC_DIR}/util/internal/protostream_objectsource.cc
    ${GP_SRC_DIR}/util/internal/protostream_objectwriter.cc
    ${GP_SRC_DIR}/util/internal/type_info.cc
    ${GP_SRC_DIR}/util/internal/type_info_test_helper.cc
    ${GP_SRC_DIR}/util/internal/utility.cc
    ${GP_SRC_DIR}/util/json_util.cc
    ${GP_SRC_DIR}/util/message_differencer.cc
    ${GP_SRC_DIR}/util/time_util.cc
    ${GP_SRC_DIR}/util/type_resolver_util.cc
    ${GP_SRC_DIR}/wire_format.cc
    ${GP_SRC_DIR}/wrappers.pb.cc)

  add_library( protobuf-lite-static
    STATIC
    ${PROTOBUF_LITE_SRCS})
  target_compile_options(protobuf-lite-static PUBLIC "-I${PROTOBUF_SRC_DIR}/..")
  
  add_library( protobuf-lite
    SHARED 
    ${PROTOBUF_LITE_SRCS})
  extra_pb_link_options(protobuf-lite)
  target_compile_options(protobuf-lite PUBLIC "-I${PROTOBUF_SRC_DIR}/..")
  
  add_library( protobuf-static
    STATIC ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
    )
  target_compile_options(protobuf-static PUBLIC "-I${PROTOBUF_SRC_DIR}/..")
  
  add_library( protobuf
    SHARED ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
    )
  target_compile_options(protobuf PUBLIC "-I${PROTOBUF_SRC_DIR}/..")
  extra_pb_link_options(protobuf)
  
endif () # if(DEFINED BUILD_PROTOBUF_FULL_AND_LITE)

set(PROTOBUF_NANO_SRCS
  ${PROTOBUF_NANO_SRC_DIR}/pb_encode.c
  ${PROTOBUF_NANO_SRC_DIR}/pb_decode.c
  ${PROTOBUF_NANO_SRC_DIR}/pb_common.c)

add_library( protobuf-nano-static
  STATIC ${PROTOBUF_NANO_SRCS})
add_library( protobuf-nano
  SHARED ${PROTOBUF_NANO_SRCS})
extra_pb_nano_link_options(protobuf-nano)

protobuf_generate_nano_c( ${CMAKE_CURRENT_SOURCE_DIR}/proto
  proto/tuningfork.proto
  proto/tuningfork_clearcut_log.proto
  proto/eng_tuningfork.proto)

set( TUNINGFORK_SRCS
  clearcut_backend.cpp
  histogram.cpp
  prong.cpp
  uploadthread.cpp
  tuningfork.cpp
  tuningfork_c.cpp
  clearcutserializer.cpp
  protobuf_util.cpp
  ${MODPB64_DIR}/modp_b64.cc
  ${_PROTO_GENS_DIR}/nano/tuningfork.pb.c
  ${_PROTO_GENS_DIR}/nano/tuningfork_clearcut_log.pb.c
  ${_PROTO_GENS_DIR}/nano/eng_tuningfork.pb.c)

add_library( tuningfork-static
  STATIC ${TUNINGFORK_SRCS} ${PROTOBUF_NANO_SRCS})
set_target_properties( tuningfork-static PROPERTIES
  COMPILE_OPTIONS "-DPROTOBUF_NANO" )

add_library( tuningfork
  SHARED ${TUNINGFORK_SRCS} ${PROTOBUF_NANO_SRCS})
set_target_properties( tuningfork PROPERTIES COMPILE_OPTIONS "-DPROTOBUF_NANO" )
target_link_libraries( tuningfork
  log)
extra_tf_link_options( tuningfork )
