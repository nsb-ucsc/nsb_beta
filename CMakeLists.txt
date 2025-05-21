cmake_minimum_required(VERSION 3.10)
project(NSBDaemon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMPILER g++)

# Add Abseil include and library directories
include_directories("/opt/homebrew/opt/abseil/include")
link_directories("/opt/homebrew/opt/abseil/lib")

# Include Protobuf packages.
find_package(Protobuf REQUIRED)
message("Protobuf_VERSION: ${Protobuf_VERSION}")
message("Protobuf_LIBRARIES: ${Protobuf_LIBRARIES}")
message("Protobuf_INCLUDE_DIRS: ${Protobuf_INCLUDE_DIRS}")
message("Protobuf_PROTOC_EXECUTABLE: ${Protobuf_PROTOC_EXECUTABLE}")
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Generate protobuf files
set(PROTO_FILES
    nsb.proto
)
set(Protobuf_PROTOC_EXECUTABLE /opt/homebrew/opt/protobuf/bin/protoc-29.3.0)
message("Protobuf_PROTOC_EXECUTABLE changed to: ${Protobuf_PROTOC_EXECUTABLE}")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
# set(PROTOC_EXECUTABLE /opt/homebrew/opt/protobuf/bin/protoc)
# # find_program(PROTOC_EXECUTABLE protoc)
# execute_process(
#     COMMAND ${PROTOC_EXECUTABLE} --cpp_out=. ${CMAKE_CURRENT_SOURCE_DIR}/nsb.proto
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     RESULT_VARIABLE result
# )
# if(result)
#     message(FATAL_ERROR "Protobuf compilation failed.")
# else()
#     message(STATUS "Protobuf compilation succeeded.")
# endif()

# # Include the generated protobuf files
# set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/nsb.pb.cc")
# set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/nsb.pb.h")

# Add executable
add_executable(nsb_daemon 
    nsb_daemon.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# Link against protobuf
target_link_libraries(nsb_daemon
    ${Protobuf_LIBRARIES}
    absl_raw_logging_internal
    absl_log_internal_check_op
    absl_log_internal_message
    absl_log_internal_format
    absl_log_internal_conditions
    absl_log_severity
    absl_strings
)