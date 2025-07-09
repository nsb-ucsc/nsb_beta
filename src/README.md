# NSB Client API in C++

C++ 17 is required to use the client API.

## Setting Up the Library
When you build NSB with CMake, the client library is automatically compiled and can be found in the _build_ directory as **libnsb.\***.

The library contains two headers, **_nsb.h_** and **_nsb_client.h_**, under the same namespace **_nsb_**. The base header **_nsb.h_** provides common code for the NSB system and is used by **nsb_daemon**.
The client header **_nsb_client.h_** provides the API for implementing clients in the application and network simulator.

## Usage

To use just the client API, include the client header:
```
#include "nsb_client.h"
```
To use other NSB code, include the base header:
```
#include "nsb.h"
```