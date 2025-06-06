cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(NSBDaemon)

message(STATUS "Current working directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary directory: ${CMAKE_CURRENT_BINARY_DIR}")

# Get necessary packages.
find_package(gRPC CONFIG REQUIRED)
find_package(absl REQUIRED)
find_package(Protobuf REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# Use pkg-config for hiredis due to this issue (https://github.com/coturn/coturn/issues/1618)
find_package(PkgConfig REQUIRED)
pkg_check_modules(hiredis REQUIRED IMPORTED_TARGET hiredis)

# Verify that libraries have been found.
set(link_targets
    yaml-cpp::yaml-cpp
    gRPC::grpc++
    absl::base
    absl::log
    absl::time
    PkgConfig::hiredis
)
message(STATUS "Checking target libraries:")
foreach(link_target ${link_targets})
    if(TARGET ${link_target})
        message(STATUS "✓ Found target: ${link_target}")
    else()
        message(STATUS "✗ Target not found: ${link_target}")
    endif()
endforeach()
# Verify that directories are found.
message(STATUS "Directories to be included:")
message(STATUS "▸ Binary: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "▸ Protobuf: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "▸ YAML: ${YAML_CPP_INCLUDE_DIR}")

# Set up executable.
add_executable(nsb_daemon 
    nsb_daemon.cc
    nsb.cc
    nsb.proto
)
# Generate C++ protobuf files.
protobuf_generate(
    TARGET nsb_daemon
    LANGUAGE cpp
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
# Generate Python protobuf file.
protobuf_generate(
    TARGET nsb_daemon
    LANGUAGE python
    PROTOC_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries.
target_link_libraries(nsb_daemon PUBLIC
    ${link_targets}
)
# Include directories.
target_include_directories(nsb_daemon
    PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIR}
)