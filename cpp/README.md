# NSB Client API in C++

C++ 17 is required to use the client API.

## Setting Up the Library
When you build NSB with CMake, the client library is automatically compiled and 
can be found in the _build_ directory as **libnsb.\***. Upon installation, the 
library, binaries, and include directories can also be found in 
```[your/install/path]/nsb``` in the ```lib```, ```include```, and ```bin``` 
subdirectories respectively.

The library contains two headers, **_nsb.h_** and **_nsb_client.h_**, under the same namespace **_nsb_**. The base header **_nsb.h_** provides common code for the NSB system and is used by **nsb_daemon**.
The client header **_nsb_client.h_** provides the API for implementing clients in the application and network simulator.

## Basic API Usage

To use just the client API, include the client header:
```
#include "nsb_client.h"
```
To use other NSB code, include the base header:
```
#include "nsb.h"
```
_Full integration guide coming soon. Please refer to the Doxygen-generated 
documentation in the meantime._

## Compiling Your Project with NSB

To compile your C++ program on the command line, we recommend _pkg-config_
macro expansion to ensure that all necessary libraries are linked and 
directories are included.
```
clang++ -Wall -std=c++17 $(pkg-config --cflags --libs nsb) [SOURCE(S)] -o [EXECUTABLE]
```