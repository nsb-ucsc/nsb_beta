# NSB Client API in Python

## Adding the Client Module
_Installable Python package coming soon._

To access the client module, we recommend directly just copying the contents 
of this directory to your Python project and use it as a module within your 
project.

If you want to install the package in development mode and want it to be 
accessible to other project spaces, you can install it. From this directory:
```bash
pip install -e .
```
Then, add the full path to this module to your PYTHONPATH, and add that to your
_.zshrc_ or _.bashrc_ file.
```
echo 'export PYTHONPATH="${PYTHONPATH}:/.../nsb_beta/python"' >> ~/.zshrc
```

## Basic API Usage

To use the client API, import the module:
```python
import nsb_client as nsb
```

### The `MessageEntry` Object

When receiving or fetching payloads, NSB will return `None` when no payload is 
available for the operation or return a `MessageEntry` object when a payload is 
available. This object is implemented to hold metadata about the payload and 
includes the following attributes:
* `source` (`str`): the identifier for the original source of this payload
* `destination` (`str`): the identifier for the final destination of this payload
* `payload` (`bytes`): the actual payload that is being retrieved
* `payload_size` (`int`): the original size of the payload

### NSB Application Client (`NSBAppClient`)

You can initialize a `NSBAppClient` using its constructor:
```python
nsb_conn = nsb.NSBAppClient(identifier, server_address, server_port)
```
*Parameters:*
- `identifier` (`str`): A unique identifier for this NSB application client 
instance. This identifier must match the corresponding identifier used within 
the NSB system and simulator for proper coordination when simulator mode is 
PER_NODE.
- `server_address` (`str`): The network address (IP address or hostname) where 
the NSB daemon is running.
- `server_port` (`int`): The port number on which the NSB daemon is listening 
for client connections.

Upon constructing the application client, this method will initialize with the 
NSB Daemon (which must be running at time of execution), connect to the database
if configured to do so, and identify itself within the NSB system. We recommend 
having clients persist throughout the duration of a simulation in PULL mode, and
it is required in PUSH mode.

#### Sending Payloads (`send`)

You can send a payload through the `NSBAppClient`'s `send` method:
```python
nsb_conn.send(dest_id, payload)
```
*Parameters:*
- `dest_id` (`str`): The identifier of the destination NSB client that should 
receive the payload.
- `payload`: The data payload to send to the destination client.

*Returns:*
- `str`: The key returned from storing the message in the database, if database 
storage is configured. Returns `None` if no database is used.

When this method is called, it creates an NSB SEND message containing the 
destination information and payload, then transmits it to the NSB daemon. This 
is a fire-and-forget operation that does not wait for or expect a response from 
the daemon. If the NSB system is configured to use database storage, the message
will be stored and the unique key will be routed across NSB to be used for 
retrieval during the simulator client's `fetch` operation. Capturing the key at
the application client is not necessary, but can be useful for debugging.

#### Receiving Payloads (`receive`)

You can receive a payload through the `NSBAppClient`'s `receive` method:
```python
received = nsb_conn.receive()
if received:
    payload = received.payload
    # Process payload.
    ...
```
This method has a secondary signature that allows the client to receive on 
behalf of another `dest_id` and can also specify a `timeout` for server 
communication, but this signature is not necessary.
```python
received = nsb_conn.receive(dest_id, timeout)
```
*Parameters:*
- `dest_id` (`str|None`, optional): The identifier of the destination NSB client
to receive messages for. Defaults to `None`, which automatically assumes the
destination is the current client (`self`).
- `timeout` (`int|None`, optional): The amount of time in seconds to wait for 
incoming data. `None` denotes waiting indefinitely (blocking behavior), while 
`0` enables polling behavior (non-blocking). Defaults to `None`.

*Returns:*
- `MessageEntry|None`: The MessageEntry struct containing the received payload 
and metadata if a message is found, otherwise `None`.

Behavior varies by system mode:

* *In PULL mode:* This method creates an NSB RECEIVE message with the specified
destination information and sends it to the daemon. The daemon responds with either a
MESSAGE code containing the retrieved payload or a NO_MESSAGE code if no message is
available. The full MessageEntry is returned to provide access to both payload and
metadata.
* *In PUSH mode:* This method awaits incoming messages on the 
`Comms.Channels.RECV` channel using `select` with the specified timeout. For 
polling behavior, set `timeout=0`. For indefinite listening (recommended for
asynchronous listener implementations), use `timeout=None` which will block 
until a message arrives.

#### Listening for Payloads (`listen`)

NSB's Python client library provides an asynchronous method of receiving 
payloads in `listen`:
```python
async def some_coroutine(...):
    ...
    received = await nsb_conn.listen()
    if received:
        payload = received.payload
        # Process payload.
        ...
```
*Returns:*
- `MessageEntry|None`: The MessageEntry struct containing the received payload 
and metadata if a message is found, otherwise `None`.

This method is a coroutine that can be used in asynchronous calls. Its 
implementation is similar to the `receive` method, but leverages asynchronous
listening instead. As the name suggests, this method is recommended for 
implementing asynchronous listener logic.

### NSB Simulator Client (`NSBSimClient`)

You can initialize a `NSBSimClient` using its constructor in pretty much the 
same way as its application counterpart:
```python
nsb_conn = nsb.NSBSimClient(identifier, server_address, server_port)
```
*Parameters:*
- `identifier` (`str`): A unique identifier for this NSB simulator client 
instance. This identifier must match the corresponding identifier used within 
the NSB system and application client for proper coordination when simulator 
mode is PER_NODE.
- `server_address` (`str`): The network address (IP address or hostname) where 
the NSB daemon is running.
- `server_port` (`int`): The port number on which the NSB daemon is listening 
for client connections.

Just like the application client, upon constructing the application client, this
method will initialize with the NSB Daemon (which must be running at time of 
execution), connect to the database if configured to do so, and identify itself 
within the NSB system.

**NOTE:** When the simulator mode is set to SYSTEM_WIDE, only one simulator 
client can connect to the daemon, as it as assumed that there is a global 
simulator client being used in the system. Also, as with the application client,
we recommend having clients persist throughout the duration of a simulation in 
PULL mode, and it is required in PUSH mode.

#### Fetching Payloads for Simulation (`fetch`)

To fetch messages to transmit over the simulated network, you can use the 
`fetch` method:
```python
fetched = nsb_conn.fetch()
if fetched:
    src_id, dest_id, payload = fetched.source, fetched.destination, fetched.payload
    # Transmit over the simulated network.
    ...
```
*Parameters:*
- `src_id` (`str|None`, optional): The identifier of the target source to fetch 
messages from. Defaults to `None`, which will result in fetching the most recent
message regardless of source, though this can be overwritten (see description below).
- `timeout` (`int|None`, optional): The amount of time in seconds to wait to 
receive data. `None` denotes waiting indefinitely (blocking behavior), while 
`0` denotes polling behavior (non-blocking). Defaults to `None` and gets 
overwritten with the system default response timeout time.

*Returns:*
- `MessageEntry|None`: The MessageEntry struct containing the fetched payload 
and metadata if a message is found, otherwise `None`.

Like with the `NSBAppClient.receive` method, behavior varies based on mode, In 
system PULL mode, When this method is called, it creates an NSB FETCH message with the specified 
source information and sends it to the daemon. The daemon responds with either 
a MESSAGE code containing the fetched payload or a NO_MESSAGE code if no 
message is available. If a message is found, the complete MessageEntry struct is
returned to provide access to both the payload and associated metadata. In PUSH mode,
this method just awaits the forwarded payloads.

When the system simulator mode is PER_NODE, the `src_id` gets overwritten with
the client's own identifier, as it is only fetching on its own behalf.

#### Listening to Fetch Payloads (`listen`)

NSB's Python client library provides an asynchronous method of fetching 
payloads in `listen`, similar to the method of the same name in `NSBAppClient`:
```python
async def some_simulator_coroutine(...):
    ...
    received = await nsb_conn.listen()
    if received:
        src_id, dest_id, payload = received.src_id, received_dest_id, received.payload
        # Send payload from source to destination in network.
        ...
```
*Returns:*
- `MessageEntry|None`: The MessageEntry struct containing the fetched payload 
and metadata if a message is found, otherwise `None`.

This method is a coroutine that can be used in asynchronous calls. Its 
implementation is similar to the `fetch` method, but leverages asynchronous
listening instead. As the name suggests, this method is recommended for 
implementing asynchronous fetcher logic.

#### Posting an Arrived Payload (`post`)

When a payload arrives at the destination node in the simulated network, you can
inform NSB and make it available for receiving on the `NSBAppClient` side of 
things:
```python
# Payload arrived at destination.
...
nsb_conn.post(src_id, dest_id, payload)
```
*Parameters:*
- `src_id` (`str`): The identifier of the source NSB client.
- `dest_id` (`str`): The identifier of the destination NSB client.
- `payload` (`bytes`): The payload data to post to the destination.

This method is intended to be used when a payload has finished being processed 
and the simulator client needs to hand it back to NSB. When called, it creates 
an NSB POST message containing the source, destination, and payload information,
then transmits it to the daemon.

## Additional Documentation via Doxygen
The code has been commented with Doxygen-style comment blocks for your 
convenience. You can use Doxygen to generate documentation as you wish.