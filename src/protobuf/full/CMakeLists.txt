cmake_minimum_required(VERSION 3.4.1)

include( ../protobuf.cmake )

include_directories(${PROTOBUF_SRC_DIR})
include_directories(${PROTOBUF_SRC_DIR}/..)

add_library( protobuf-static
  STATIC ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
)

add_library( protobuf
  SHARED ${PROTOBUF_LITE_SRCS} ${PROTOBUF_SRCS}
)
extra_pb_link_options(protobuf)
