cmake_minimum_required(VERSION 3.10)
project(NSBDaemon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMPILER g++)

# Include Protobuf packages.
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Generate protobuf files
set(PROTO_FILES
    nsb.proto
)
# protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Add executable
add_executable(nsb_daemon 
    nsb_daemon.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# Link against protobuf
target_link_libraries(nsb_daemon ${Protobuf_LIBRARIES})