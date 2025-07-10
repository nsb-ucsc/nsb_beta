cmake_minimum_required(VERSION 3.10)
# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Project information.
project(nsb
    VERSION 0.1.0
    DESCRIPTION "NSB – Network Simulation Bridge"
    HOMEPAGE_URL "https://github.com/nsb-ucsc/nsb_beta"
    LANGUAGES CXX
)

# Set standard install directories.
include(GNUInstallDirs)

# Make cpp/proto and python/proto directories.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/cpp/proto)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/python/proto)

# Set directories as necessary.
message(STATUS "Source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "Binary directory: ${CMAKE_BINARY_DIR}")
set(CPP_DIR "${CMAKE_SOURCE_DIR}/cpp")
set(CPP_SRC_DIR "${CPP_DIR}/src")
set(CPP_INCLUDE_DIR "${CPP_DIR}/include")
set(CPP_PROTO_DIR "${CPP_DIR}/proto")
set(PYTHON_DIR "${CMAKE_SOURCE_DIR}/python")
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
message(STATUS "C++ directory: ${CPP_DIR}")
message(STATUS "▸ Source: ${CPP_SRC_DIR}")
message(STATUS "▸ Headers: ${CPP_INCLUDE_DIR}")
message(STATUS "▸ Proto: ${CPP_PROTO_DIR}")
message(STATUS "Python directory: ${PYTHON_DIR}")
message(STATUS "Proto File directory: ${PROTO_DIR}")

# Get necessary packages.
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
    protobuf::libprotobuf
    absl::base
    absl::log
    absl::time
    absl::log_internal_check_op
    absl::log_initialize
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
message(STATUS "Protobuf Libraries: ${Protobuf_LIBRARIES}")
# Verify that directories are found.
message(STATUS "Directories to be included:")
message(STATUS "▸ Source: ${CMAKE_SOURCE_DIR}")
message(STATUS "▸ Protobuf: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "▸ YAML: ${YAML_CPP_INCLUDE_DIR}")

# Set up library.
add_library(nsb SHARED
    ${CPP_SRC_DIR}/nsb.cc
    ${CPP_SRC_DIR}/nsb_client.cc
)
# Link libraries.
target_link_libraries(nsb PUBLIC
    ${link_targets}
)
# Include directories.
target_include_directories(nsb PUBLIC
    ${CPP_INCLUDE_DIR}
    ${CPP_PROTO_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIR}
)

message(STATUS "Protoc Executable: ${Protobuf_PROTOC_EXECUTABLE}")
# Generate C++ protobuf files.
protobuf_generate(
    TARGET nsb
    LANGUAGE cpp
    IMPORT_DIRS ${PROTO_DIR}
    PROTOC_OUT_DIR ${CPP_DIR}/proto
    PROTOS ${PROTO_DIR}/nsb.proto
)
# Generate Python protobuf file.
protobuf_generate(
    TARGET nsb
    LANGUAGE python
    IMPORT_DIRS ${PROTO_DIR}
    PROTOC_OUT_DIR ${PYTHON_DIR}/proto
    PROTOS ${PROTO_DIR}/nsb.proto
)
# Cleaning up.
set_target_properties(nsb PROPERTIES ADDITIONAL_CLEAN_FILES
    "${CPP_DIR}/proto/*;${PYTHON_DIR}/proto/*;${PYTHON_DIR}/__pycache__/*"
)

# Compile daemon executable.
add_executable(nsb_daemon ${CPP_SRC_DIR}/nsb_daemon.cc)
# Link NSB library.
target_link_libraries(nsb_daemon PUBLIC nsb)
# Include directories.
target_include_directories(nsb_daemon PUBLIC 
    ${CPP_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIR}
)

# Generate pkg-config file.
configure_file(
    ${CMAKE_SOURCE_DIR}/nsb.pc.in
    ${CMAKE_BINARY_DIR}/nsb.pc
    @ONLY
)

# Install the pkg-config file.
install(FILES ${CMAKE_BINARY_DIR}/nsb.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    COMPONENT development
)

# Install libraries and headers.
install(TARGETS nsb
    EXPORT nsbTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT runtime
)

# Install your headers.
install(DIRECTORY ${CPP_INCLUDE_DIR}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsb
    COMPONENT development
    FILES_MATCHING PATTERN "*.h"
)

### FOR TESTING ###

# Compile nsb_test.
add_executable(nsb_test ${CPP_DIR}/nsb_test.cc)
target_link_libraries(nsb_test PUBLIC nsb)
target_include_directories(nsb_test PUBLIC 
    ${CPP_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIR}
)
