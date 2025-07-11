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
* _**Pkg-Config**_, necessary in MacOS for package configuration
* **Redis Server**, used as a database to store payloads
* **Abseil**, necessary for Protobuf support and logging
* **Protobuf**, used to define and compile
* **YAML parsing**, to parse configuration files
* **hiredis**, to connect to the Redis server

Python API support involves additional package installation through its
[_requirements.txt_](python/requirements.txt) file and is detailed in the 
[Python README](python/README.md).

Platform-specific package install commands are provided below. Also, note that
previous installations of tools like _gRPC_ that include _protobuf_ may result
in conflicting versions for _protobuf_.

#### MacOS via Homebrew
```
brew install cmake pkg-config abseil protobuf yaml-cpp redis hiredis
```

#### Windows via vcpkg
_Coming soon._

#### Linux Distributions
_Coming soon._

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
├── cpp/
├── CMakeLists.txt
├── config.yaml
└── README.md
```

Then, enter the new _build_ directory (```cd build```) and start by configuring 
the _CMake_ build:
```cmake ..```

Within the output, you should see something like this:
```
[cmake] -- Checking target libraries:
[cmake] -- ✓ Found target: yaml-cpp::yaml-cpp
[cmake] -- ✓ Found target: protobuf::libprotobuf
[cmake] -- ✓ Found target: absl::base
[cmake] -- ✓ Found target: absl::log
[cmake] -- ✓ Found target: absl::time
[cmake] -- ✓ Found target: absl::log_internal_check_op
[cmake] -- ✓ Found target: absl::log_initialize
[cmake] -- ✓ Found target: PkgConfig::hiredis
```
If all the prerequisite software was installed, you may continue with building 
and installing NSB.
```
cmake --build . --clean-first
```
```
cmake --install .
```
The library, includes, and binary directories should now be available under
```[your/install/path]/nsb```; this also means they can be removed by deleting
this folder.

The install command will also make NSB available on _pkg-config_ 
as ```nsb```, which may be of use when compiling projects with NSB.

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

## C++
To compile your C++ program on the command line, we recommend _pkg-config_
macro expansion to ensure that all necessary libraries are linked and 
directories are included.
```
clang++ -Wall -std=c++17 $(pkg-config --cflags --libs nsb) [SOURCE(S)] -o [EXECUTABLE]
```

## Extensibility
_Coming soon._

## _Acknowledgments_

## _License_