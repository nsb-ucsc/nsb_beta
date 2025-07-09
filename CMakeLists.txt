cmake_minimum_required(VERSION 3.10)
# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(NSBDaemon)

# Set directories as necessary.
message(STATUS "Current source directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary directory: ${CMAKE_CURRENT_BINARY_DIR}")
set(CPP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PYTHON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/python")
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
message(STATUS "C++ source directory: ${CPP_SRC_DIR}")
message(STATUS "Python source directory: ${PYTHON_DIR}")
message(STATUS "Protobuf directory: ${PROTO_DIR}")

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

# Set up library.
add_library(nsb SHARED
    src/nsb.cc
    src/nsb.h
    src/nsb_client.cc
    src/nsb_client.h
)
# Link libraries.
target_link_libraries(nsb PUBLIC
    ${link_targets}
)
# Include directories.
target_include_directories(nsb PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIR}
)
# Generate C++ protobuf files.
protobuf_generate(
    TARGET nsb
    LANGUAGE cpp
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
    PROTOS ${PROTO_DIR}/nsb.proto
)
# Generate Python protobuf file.
protobuf_generate(
    TARGET nsb
    LANGUAGE python
    PROTOC_OUT_DIR ${PYTHON_DIR}
    PROTOS ${PROTO_DIR}/nsb.proto
)
# Cleaning up.
set_target_properties(${exec_target} PROPERTIES
    ADDITIONAL_CLEAN_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/proto;${PYTHON_DIR}/proto;${PYTHON_DIR}/__pycache__"
)

set(EXEC_TARGETS
    nsb_daemon
    nsb_test
)

foreach(exec_target ${EXEC_TARGETS})
    # Set up executable.
    add_executable(${exec_target}
        src/${exec_target}.cc
        # src/nsb.cc
    )

    # Link libraries.
    target_link_libraries(${exec_target} PUBLIC
        nsb
    )
    # Include directories.
    target_include_directories(${exec_target} PUBLIC 
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Protobuf_INCLUDE_DIRS}
        ${YAML_CPP_INCLUDE_DIR}
    )
endforeach(exec_target ${EXEC_TARGETS})