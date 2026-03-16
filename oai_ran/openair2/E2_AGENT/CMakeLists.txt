add_library(e2_agent STATIC
  e2_agent_app.c
  e2_message_handlers.c
  oai-oran-protolib/builds/ran_messages.pb-c.c
)

target_include_directories(e2_agent
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/oai-oran-protolib
    ${CMAKE_CURRENT_SOURCE_DIR}/oai-oran-protolib/builds
)

find_library(PROTOBUF_C_LIB protobuf-c)
if(NOT PROTOBUF_C_LIB)
  message(FATAL_ERROR "protobuf-c library not found")
endif()

target_link_libraries(e2_agent PUBLIC ${PROTOBUF_C_LIB} pthread)
