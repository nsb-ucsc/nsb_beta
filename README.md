# **nsb** – the network simulation bridge
_This tool is in beta and your feedback is greatly appreciated._

The Network Simulation Bridge, or NSB for short, a simple, low-overhead pipeline
consisting of a message server and client interface libraries that bridge 
together applications and network simulators. NSB is application-, network 
simulator-, and platform-agnostic that allows developers to integrate any 
application front-end with any network simulator back-end.

For more information, or to cite NSB, you can access our 
[publication](https://dl.acm.org/doi/10.1145/3616391.3622771).

## Installation

### Prerequisites

The following software packages are required to be installed:
* **CMake**, used to configure and build the project
* **Redis Server**, used as a database to store payloads
* **Protobuf Compiler**, used to compile message definitions
* **gRPC**, used with Protobuf and Abseil
* **Abseil** _(typically included with gRPC)_, used for logging
* **YAML parsing**, to parse configuration files
* **hiredis**, to connect to the Redis server
* _**Pkg-Config**_, necessary in MacOS for package configuration

Python API support involves additional package installation through its
[_requirements.txt_](python/requirements.txt) file and is detailed in the 
[Python README](python/README.md).

Platform-specific package install commands are provided below.

#### MacOS via Homebrew
```
brew install cmake yaml-cpp grpc abseil hiredis pkg-config
```

#### Windows via vcpkg
```
vcpkg install protobuf yaml-cpp grpc abseil hiredis
```

#### Linux Distributions
For Ubuntu:
```
sudo apt install cmake protobuf-compiler libyaml-cpp-dev libgrpc++-dev libabsl-dev libhiredis-dev pkg-config
```

For Fedora (also for CentOS using _yum_):
```
sudo dnf install cmake protobuf-devel protobuf-compiler yaml-cpp-devel grpc-devel abseil-cpp-devel hiredis-devel pkgconfig
```

With Linux, you will have to install gRPC with Protocol Buffers from source
using CMake; that process is detailed [here](https://grpc.io/docs/languages/cpp/quickstart/).

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

These instructions assume you have already implemented the client-side APIs in
your code. Once this is complete, we recommend taking these steps in order:

1. **Start the Redis server.** Specify or take note of the port number that the
server is running on and make sure your configuration file points to the address
and port:
```redis-server -p 5050```

2. **Start the NSB Daemon.** If you followed the build instructions in above, 
then you can start the NSB Daemon executable from the _build_ directory: 
```./build/nsb_daemon```

3. **Start the modified network simulator.** In most cases, the simulator, 
modified using the _NSBSimClient_ API, should be started before the application
in order to be ready and listening for messages from the application space.

4. **Run your modified application.** Using the _NSBAppClient_ API, your 
application(s) should now send messages via NSB over the simulated network.

Once the Redis server and NSB Daemon are running in the background, you can

### Application Programming Interfaces
Currently, we support and provide interfaces for [Python](python/README.md) and 
[C++](src/README.md).

## Extensibility
_Coming soon._

## _Acknowledgments_

## _License_