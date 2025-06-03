cmake_minimum_required(VERSION 3.10)
project(NSBDaemon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMPILER g++)

# Add Abseil include and library directories
include_directories("/opt/homebrew/opt/abseil/include")
link_directories("/opt/homebrew/opt/abseil/lib")



# Get YAML, Protobuf and Python packages.
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp REQUIRED)
message(STATUS "YAML_CPP_INCLUDE_DIR: ${YAML_CPP_INCLUDE_DIR}")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

message(STATUS "Current working directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary directory: ${CMAKE_CURRENT_BINARY_DIR}")

include_directories(${YAML_CPP_INCLUDE_DIR})

# Generate protobuf files.
find_program(PROTOBUF_PROTOC protoc REQUIRED)
message(STATUS "Protobuf compiler: ${PROTOBUF_PROTOC}")

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/nsb.proto")
message(STATUS "Protobuf file: ${PROTO_FILE}")
set(protoc_args 
    "--cpp_out=${CMAKE_CURRENT_BINARY_DIR}"
    "--python_out=${CMAKE_CURRENT_BINARY_DIR}"
    "-I"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/nsb.proto"
)
message(STATUS "Protobuf arguments: ${protoc_args}")

set(PB_CPP_OUT "${CMAKE_CURRENT_BINARY_DIR}/nsb.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/nsb.pb.h")
set(PB_PY_OUT "${CMAKE_CURRENT_BINARY_DIR}/nsb_pb2.py")

# Create custom command.
add_custom_command(
    OUTPUT ${PB_CPP_OUT} ${PB_PY_OUT}
    COMMAND ${PROTOBUF_PROTOC}
    ARGS ${protoc_args}
    DEPENDS ${PROTO_FILE}
)
set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
# Create and link libraries.




# Add executable
add_executable(nsb_daemon 
    nsb_daemon.cc
    ${PB_CPP_OUT}
)

target_link_libraries(nsb_daemon
    ${Protobuf_LIBRARIES}
    absl_raw_logging_internal
    absl_log_internal_check_op
    absl_log_internal_message
    absl_log_internal_format
    absl_log_internal_conditions
    absl_log_severity
    absl_strings
    "/opt/homebrew/lib/libyaml-cpp.dylib"
)
target_include_directories(nsb_daemon PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)