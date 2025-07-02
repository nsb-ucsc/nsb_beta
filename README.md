# **nsb** – the network simulation bridge
_This tool is in beta and your feedback is greatly appreciated._

Summary.

Paper.

## Installation

### Prerequisites

The following software packages are required to be installed:
* gRPC – ```grpc```
* Abseil _(typically included with gRPC)_ – ```abseil```
* YAML for C++ – ```yaml-cpp```
* hiredis – ```hiredis```
* Pkg-Config _(required for hiredis on MacOS)_ – ```hiredis```


### Build
__Cmake__ is used to build this project. In order to build the NSB components (
the NSB Daemon executable and the C++ and Python client libraries), create a 
_build_ (```mkdir build```) directory at the top level of this project 
directory, such that your directory now looks like this:

```
nsb/
├── build/
├── proto/
├── python/
├── src/
├── CMakeLists.txt
├── config.yaml
└── README.md
```

Then, enter the new _build_ directory (```cd build```) and run the following 
_CMake_ commands:

```cmake -DCMAKE_BUILD_TYPE=Debug ..```

```cmake --build . --clean-first```

If all the prerequisite software was installed, this should work without issue.
Within the _CMake_ build process output, you should see something like this:

```
-- Checking target libraries:
-- ✓ Found target: yaml-cpp::yaml-cpp
-- ✓ Found target: gRPC::grpc++
-- ✓ Found target: absl::base
-- ✓ Found target: absl::log
-- ✓ Found target: absl::time
-- ✓ Found target: PkgConfig::hiredis
```

The checkmarks indicate that the required software has been found successfully.

## Usage

1. **Start the NSB Daemon.** If you followed the build instructions in above, 
then you can start the NSB Daemon from the _build_ directory --- ```./build/nsb_daemon```

### C++ API

### Python API

## Extensibility

## _Acknowledgments_

## _License_