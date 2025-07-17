cmake_minimum_required(VERSION 3.10)

project(nsb
    VERSION 0.1.0
    DESCRIPTION "NSB – Network Simulation Bridge"
    HOMEPAGE_URL "https://github.com/nsb-ucsc/nsb_beta"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

# ------------------------------------------------------------------
# Source tree layout
# ------------------------------------------------------------------
set(CPP_DIR          "${CMAKE_SOURCE_DIR}/cpp")
set(CPP_SRC_DIR      "${CPP_DIR}/src")
set(CPP_INCLUDE_DIR  "${CPP_DIR}/include")
set(PROTO_DIR        "${CMAKE_SOURCE_DIR}/proto")
set(PYTHON_DIR       "${CMAKE_SOURCE_DIR}/python")  # for install of generated py stubs

# ------------------------------------------------------------------
# Generated protobuf output dirs (build tree)
# ------------------------------------------------------------------
set(NSB_GEN_CPP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp")
set(NSB_GEN_PY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/generated/python")
file(MAKE_DIRECTORY "${NSB_GEN_CPP_DIR}" "${NSB_GEN_PY_DIR}")

# ------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------
find_package(Protobuf REQUIRED)          # 27.5 installed; version check omitted
find_package(absl REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(hiredis REQUIRED IMPORTED_TARGET hiredis)

# ------------------------------------------------------------------
# NSB library
# ------------------------------------------------------------------
add_library(nsb SHARED
    "${CPP_SRC_DIR}/nsb.cc"
    "${CPP_SRC_DIR}/nsb_client.cc"
    # nsb.pb.cc appended by protobuf_generate()
)

# Public include dirs: core headers + generated protobuf headers
target_include_directories(nsb
  PUBLIC
    "${CPP_INCLUDE_DIR}"
    "${NSB_GEN_CPP_DIR}"
    "${NSB_GEN_CPP_DIR}/proto"   # where nsb.pb.h ends up
)

# Link libs (expanded Abseil list to satisfy linker)
target_link_libraries(nsb
  PUBLIC
    protobuf::libprotobuf
    yaml-cpp::yaml-cpp
    absl::log
    absl::log_initialize          # <-- fixes undefined absl::InitializeLog()
    absl::log_internal_check_op   # <-- fixes CheckOpMessageBuilder symbol
    absl::raw_logging_internal
    absl::strings
    absl::base
    absl::time
    PkgConfig::hiredis
)

# SQLite target guard
if (TARGET SQLite::SQLite3)
  target_link_libraries(nsb PUBLIC SQLite::SQLite3)
else()
  target_link_libraries(nsb PUBLIC sqlite3)
endif()

# ------------------------------------------------------------------
# Protobuf code generation (C++ + Python)
# ------------------------------------------------------------------
protobuf_generate(
    TARGET nsb
    LANGUAGE cpp
    IMPORT_DIRS "${PROTO_DIR}"
    PROTOC_OUT_DIR "${NSB_GEN_CPP_DIR}"
    PROTOS "${PROTO_DIR}/nsb.proto"
)

protobuf_generate(
    TARGET nsb
    LANGUAGE python
    IMPORT_DIRS "${PROTO_DIR}"
    PROTOC_OUT_DIR "${NSB_GEN_PY_DIR}"
    PROTOS "${PROTO_DIR}/nsb.proto"
)

# Clean up generated files on "make clean"
set_property(TARGET nsb APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${NSB_GEN_CPP_DIR};${NSB_GEN_PY_DIR}"
)

# ------------------------------------------------------------------
# NSB daemon executable
# ------------------------------------------------------------------
add_executable(nsb_daemon "${CPP_SRC_DIR}/nsb_daemon.cc")
target_link_libraries(nsb_daemon PRIVATE nsb)
target_include_directories(nsb_daemon PRIVATE
    "${CPP_INCLUDE_DIR}"
    "${NSB_GEN_CPP_DIR}"
    "${NSB_GEN_CPP_DIR}/proto"
)

# ------------------------------------------------------------------
# nsb_test (optional)
# ------------------------------------------------------------------
if (EXISTS "${CPP_DIR}/nsb_test.cc")
  add_executable(nsb_test "${CPP_DIR}/nsb_test.cc")
  target_link_libraries(nsb_test PRIVATE nsb)
  target_include_directories(nsb_test PRIVATE
      "${CPP_INCLUDE_DIR}"
      "${NSB_GEN_CPP_DIR}"
      "${NSB_GEN_CPP_DIR}/proto"
  )
endif()

# ------------------------------------------------------------------
# Installation layout  (/usr/local/nsb/...)
# ------------------------------------------------------------------
set(NSB_INSTALL_LIBDIR      "nsb/${CMAKE_INSTALL_LIBDIR}")
set(NSB_INSTALL_BINDIR      "nsb/${CMAKE_INSTALL_BINDIR}")
set(NSB_INSTALL_INCLUDEDIR  "nsb/${CMAKE_INSTALL_INCLUDEDIR}")

# Ensure rpath install name for macOS
set_target_properties(nsb PROPERTIES
    INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/${NSB_INSTALL_LIBDIR}"
)

# pkg-config file
configure_file(
    "${CMAKE_SOURCE_DIR}/nsb.pc.in"
    "${CMAKE_BINARY_DIR}/nsb.pc"
    @ONLY
)

install(FILES "${CMAKE_BINARY_DIR}/nsb.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    COMPONENT development
)

install(TARGETS nsb nsb_daemon
    EXPORT nsbTargets
    LIBRARY DESTINATION "${NSB_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${NSB_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${NSB_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${NSB_INSTALL_INCLUDEDIR}"
    COMPONENT runtime
)

install(DIRECTORY "${CPP_INCLUDE_DIR}/"
    DESTINATION "${NSB_INSTALL_INCLUDEDIR}"
    COMPONENT development
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY "${NSB_GEN_CPP_DIR}/"
    DESTINATION "${NSB_INSTALL_INCLUDEDIR}"
    COMPONENT development
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY "${NSB_GEN_PY_DIR}/"
    DESTINATION "${NSB_INSTALL_BINDIR}/python_proto"
    COMPONENT runtime
    FILES_MATCHING PATTERN "*.py"
)

# ------------------------------------------------------------------
# Status output (optional)
# ------------------------------------------------------------------
message(STATUS "Protoc Executable: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Using generated C++ protobuf dir: ${NSB_GEN_CPP_DIR}")
message(STATUS "Using generated Python protobuf dir: ${NSB_GEN_PY_DIR}")

