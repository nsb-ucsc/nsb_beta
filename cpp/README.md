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

## Compiling Your Project with NSB

To compile your C++ program on the command line, we recommend _pkg-config_
macro expansion to ensure that all necessary libraries are linked and 
directories are included.
```
clang++ -Wall -std=c++17 $(pkg-config --cflags --libs nsb) [SOURCE(S)] -o [EXECUTABLE]
```

## Basic API Usage

To use just the client API, include the client header:
```
#include "nsb_client.h"
```
To use other NSB code (maybe for development or debugging), include the base header:
```
#include "nsb.h"
```

All NSB client functionality is contained within the `nsb` namespace:
```cpp
using namespace nsb;
```

### Notes on the C++ Implementation

#### Representing Bytestrings as `string`

In our implementation, we make the choice to use well-formed `string` types to
represent the payload bytestrings, constructed from the the reception buffer with
specific lengths. We do this with the intention of keeping all data as is and for
better object and memory management. In some simulators, like OMNeT++ without INET, null characters (`\00`) within payloads are not properly handled and can terminate a character array or string. We are working on a solution to this and are open to
feedback.

#### The `MessageEntry` Object

When receiving or fetching payloads, NSB methods will return an empty `MessageEntry` when no payload is available for the operation or return a populated `MessageEntry` object when a payload is available. This object holds metadata about the payload and includes the following attributes:
* `source` (`std::string`): the identifier for the original source of this payload
* `destination` (`std::string`): the identifier for the final destination of this payload  
* `payload` (`std::string`): the actual payload that is being retrieved
* `payload_size` (`int`): the original size of the payload
The `MessageEntry` struct also includes a method – `exists()` – to tell you 
whether a payload exists (the object is populated) or doesn't (the object is empty).
```cpp
MessageEntry received = nsb_Conn.receive();
if (received.exists()) {
    std::string payload = received.payload;
    // Process payload.
    ...
}
```

### NSB Application Client (`NSBAppClient`)

You can initialize an `NSBAppClient` using its constructor:
```cpp
NSBAppClient nsb_conn(identifier, serverAddress, serverPort);
```
*Parameters:*
- `identifier` (`const std::string&`): A unique identifier for this NSB application client instance. This identifier must match the corresponding identifier used within the NSB system and simulator for proper coordination when simulator mode is PER_NODE.
- `serverAddress` (`std::string&`): The network address (IP address or hostname) where the NSB daemon is running.
- `serverPort` (`int`): The port number on which the NSB daemon is listening for client connections.

Upon constructing the application client, this method will initialize with the 
NSB Daemon (which must be running at time of execution), connect to the database
if configured to do so, and identify itself within the NSB system. We recommend 
having clients persist throughout the duration of a simulation in PULL mode, and
it is required in PUSH mode.

#### Sending Payloads (`send`)

You can send a payload through the `NSBAppClient`'s `send` method:
```cpp
nsb_conn.send(dest_id, payload);
```
*Parameters:*
- `dest_id` (`const std::string`): The identifier of the destination NSB client that should receive the payload.
- `payload` (`std::string`): The data payload to send to the destination client.

*Returns:*
- `std::string`: The key returned from storing the message in the database, if database storage is configured. Returns empty string if no database is used.

When this method is called, it creates an NSB SEND message containing the destination information and payload, then transmits it to the NSB daemon.
This is a fire-and-forget operation that does not wait for or expect a response from the daemon. If the NSB system is configured to use database storage, the message
will be stored and the unique key will be routed across NSB to be used for 
retrieval during the simulator client's `fetch` operation. Capturing the key at
the application client is not necessary, but can be useful for debugging.

#### Receiving Payloads (`receive`)

You can receive a payload through the `NSBAppClient`'s `receive` method:
```cpp
MessageEntry received = nsb_conn.receive();
if (!received.payload.empty()) {
    std::string payload = received.payload;
    // Process payload.
    ...
}
```

This method has additional signatures that allow specifying a destination ID and timeout:
```cpp
MessageEntry received = nsb_conn.receive(&dest_id, timeout);
MessageEntry received = nsb_conn.receive(timeout);
```
*Parameters:*
- `dest_id` (`std::string*`, optional): Pointer to the identifier of the destination NSB client to receive messages for. Pass `nullptr` to automatically assume the destination is the current client.
- `timeout` (`int`, optional): The amount of time in seconds to wait for incoming data. Defaults to `DAEMON_RESPONSE_TIMEOUT`. Use `0` for polling behavior (non-blocking).

*Returns:*
- `MessageEntry`: The MessageEntry struct containing the received payload and metadata if a message is found, otherwise an empty MessageEntry.

Behavior varies by system mode:
* *In PULL mode:* This method creates an NSB RECEIVE message with the specified destination information and sends it to the daemon. The daemon responds with either a MESSAGE code containing the retrieved payload or a NO_MESSAGE code if no message is available.
* *In PUSH mode:* This method awaits incoming messages on the communication channel with the specified timeout. For polling behavior, the timeout is automatically set to 0.

### NSB Simulator Client (`NSBSimClient`)

You can initialize an `NSBSimClient` using its constructor in the same way as its application counterpart:
```cpp
NSBSimClient nsb_conn(identifier, serverAddress, serverPort);
```
*Parameters:*
- `identifier` (`const std::string&`): A unique identifier for this NSB simulator client instance. This identifier must match the corresponding identifier used within the NSB system and application client for proper coordination when simulator mode is PER_NODE.
- `serverAddress` (`std::string&`): The network address (IP address or hostname) where the NSB daemon is running.
- `serverPort` (`int`): The port number on which the NSB daemon is listening for client connections.

Just like the application client, upon constructing the simulator client, initialization establishes connection with the NSB Daemon, connect to the database if configured, and identify itself within the NSB system.

**NOTE:** When the simulator mode is set to SYSTEM_WIDE, only one simulator client can connect to the daemon, as it is assumed that there is a global simulator client being used in the system. We recommend having clients persist throughout the duration of a simulation in PULL mode, and it is required in PUSH mode.

#### Fetching Payloads for Simulation (`fetch`)

To fetch messages to transmit over the simulated network, you can use the `fetch` method:
```cpp
MessageEntry fetched = nsb_conn.fetch()
if (fetched.exists()) {
    std::string payload = fetched.payload;
    std::string src_id = fetched.source;
    std::string dest_id = fetched.destination;
    // Transmit over the simulated network.
    ...
}
```
This method has additional signatures:
```cpp
MessageEntry fetched = nsb_conn.fetch(&src_id, timeout);
MessageEntry fetched = nsb_conn.fetch(timeout);
```
*Parameters:*
- `src_id` (`std::string*`, optional): Pointer to the identifier of the target source to fetch messages from. Pass `nullptr` to fetch the most recent message regardless of source.
- `timeout` (`int`, optional): The amount of time in seconds to wait to receive data. Defaults to `DAEMON_RESPONSE_TIMEOUT`. Use `0` for polling behavior (non-blocking).

*Returns:*
- `MessageEntry`: The MessageEntry struct containing the fetched payload and metadata if a message is found, otherwise an empty MessageEntry.

When this method is called, it creates an NSB FETCH message with the specified source information and sends it to the daemon. The daemon responds with either a MESSAGE code containing the fetched payload or a NO_MESSAGE code if no message is available.

When the system simulator mode is PER_NODE, the `src_id` gets overwritten with the client's own identifier, as it is only fetching on its own behalf.

#### Posting an Arrived Payload (`post`)

When a payload arrives at the destination node in the simulated network, you can inform NSB and make it available for receiving on the `NSBAppClient` side:
```cpp
// Payload arrived at destination.
...
std::string result = nsb_conn.post(src_id, dest_id, payload);
```
*Parameters:*
- `src_id` (`std::string`): The identifier of the source NSB client.
- `dest_id` (`std::string`): The identifier of the destination NSB client.
- `payload` (`std::string&`): The payload data to post to the destination.

*Returns:*
- `std::string`: Result status or key from the post operation.

This method is intended to be used when a payload has finished being processed and the simulator client needs to hand it back to NSB. When called, it creates an NSB POST message containing the source, destination, and payload information, then transmits it to the daemon.

## _Notes_
### Additional Documentation via Doxygen
The code has been commented with Doxygen-style comment blocks for your convenience. You can use Doxygen to generate comprehensive API documentation as needed.
### Use of AI
Anthropic's Claude Connet 4 was used to partially generate this documentation based on internal documentation of the code and was heavily edited by a human contributor.
We take full responsibility for the documentation provided here.