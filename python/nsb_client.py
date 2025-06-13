import socket
import select
import time
import proto.nsb_pb2 as nsb_pb2
import asyncio

# Set up logging.
import logging
from enum import IntEnum
import threading

import redis

import random

## @cond
logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s.%(msecs)03d\t%(name)s\t%(levelname)s\t%(message)s',
                    datefmt='%H:%M:%S',
                    handlers=[logging.StreamHandler(),])
## @endcond

"""
@file nsb_client.py
@namespace nsb_client
@brief Application & Simulator Client Interfaces for NSB
@details Interfaces to facilitate communication with the NSB Daemon to 
         facilitate independent applications' payloads being routed through 
         simulated networks. The NSB Application Client provides applications 
         with the ability to send and receive payloads, similar to basic network
         interfaces, while the NSB Simulator Client provides network simulators 
         with the ability to fetch sent payloads to deploy on the simulated 
         network and post the delivery (or lack of delivery) of the payloads.
"""

## @var SERVER_CONNECTION_TIMEOUT
# Maximum time a client will wait to connect to the daemon.
SERVER_CONNECTION_TIMEOUT = 10
## @var DAEMON_RESPONSE_TIMEOUT
# Maximum time a client will wait to get a response from the daemon.
DAEMON_RESPONSE_TIMEOUT = 600
## @var RECEIVE_BUFFER_SIZE
# Buffer size when receiving data.
RECEIVE_BUFFER_SIZE = 4096
## @var SEND_BUFFER_SIZE
# Buffer size when sending data.
SEND_BUFFER_SIZE = 4096

### SYSTEM CONFIG ###

class Config:
    """
    @brief Class to maintain property codes.

    These property codes should be standardized across Python and C++ libraries, 
    including the NSB Daemon.
    """
    class SystemMode(IntEnum):
        """
        @brief Denotes whether the NSB system is in *PUSH* or *PULL* mode.

        *PULL* mode requires clients to request -- or pull -- to fetch or 
        receive incoming payloads via the daemon server's response. *PUSH* mode 
        denotes that when clients send or post outgoing payloads, they are 
        immediately forwarded to the appropriate client.

        @see NSBAppClient.receive()
        @see NSBSimClient.fetch()
        """
        PULL = 0
        PUSH = 1

    def __init__(self, nsb_msg: nsb_pb2.nsbm):
        """
        @brief Initializes configuration with a NSB message.

        @param nsb_msg The NSB INIT response message from the daemon server 
        containing the configuration. The message should have a 'config' field.

        @see NSBClient.initialize()
        """
        self.system_mode = Config.SystemMode(nsb_msg.config.sys_mode)
        self.use_db = nsb_msg.config.use_db
        if self.use_db:
            self.db_address = nsb_msg.config.db_address
            self.db_port = nsb_msg.config.db_port
            self.db_num = nsb_msg.config.db_num

    def __repr__(self):
        """
        @brief String representation of the configuration.
        """
        s = f"[CONFIG] System Mode: {self.system_mode.name} | Use DB? {self.use_db}"
        if self.use_db:
            s += f" | DB Address: {self.db_address} | DB Port: {self.db_port}"
        return s

### COMMUNICATION INTERFACES ###

class Comms:
    """
    @brief Base class for communication interfaces.

    As NSB is expected to support different communication paradigms and protocols --
    including sockets, RabbitMQ, and Websockets -- we plan to provide various 
    different communication interfaces with the same basic functions. The 
    SocketInterface class should be used as an example to develop other interfaces.
    """
    class Channels(IntEnum):
        """
        @brief Shared enumeration to designate different channels.
        """
        CTRL = 0
        SEND = 1
        RECV = 2

class SocketInterface(Comms):
    """
    @brief Socket interface for client-server communication.

    This class implements aocket interface network to facilitate network 
    communication between NSB clients and the server. This can be used as a 
    template to develop other interfaces for client communication, which must 
    define the same methods with the same arguments as done in this class.
    """
    def __init__(self, server_address: str, server_port: int):
        """
        @brief Constructor for the SocketInterface class.

        Sets the address and port of the server at the NSB daemon before 
        connecting to the server.
        
        @param server_address The address of the NSB daemon.
        @param server_port The port of the NSB daemon.

        @see nsb_client.SocketInterface._connect(timeout)
        """
        # Save connection information.
        self.server_addr = server_address
        self.server_port = server_port
        # Create logger.
        self.logger = logging.getLogger(f"SIF({self.server_addr}:{self.server_port})")
        # Initialize set of connections.
        self.conns = {}
        self._connect()

    def _connect(self, timeout:int=SERVER_CONNECTION_TIMEOUT):
        """
        @brief Connects to the daemon with the stored server address and port.

        This method configures and connects sockets for each of the client's
        channels and then attempts to connect to the daemon.

        @param timeout Maximum time in seconds to wait to connect to the daemon. 
        @exception TimeoutError Raised if the connection to the server times out 
                                after the specified timeout period.
        """
        self.logger.info(f"Connecting to daemon@{self.server_addr}:{self.server_port}...")
        # Set target time for timing out.
        target_time = time.time() + timeout
        for channel in Comms.Channels:
            self.logger.info(f"Configuring & connecting {channel.name}...")
            while time.time() < target_time:
                # Try configuring and connecting to the daemon server.
                try:
                    # Create socket connection and configure for low latency and async.
                    conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    conn.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                    # Attempt to connect.
                    conn.connect((self.server_addr, self.server_port))
                    self.conns[channel] = conn
                    break
                # If the server isn't up or reachable, wait and try again.
                except socket.error as e:
                    # self.logger.debug(f"\tFailed to connect {Comms.Channels(i).name}: {e}\n\tTrying again...")
                    self.logger.debug(f"\tRetrying connection...")
                    time.sleep(1)
            # If loop ended due to timeout, raise error, otherwise continue.
            if time.time() >= target_time:
                raise TimeoutError(f"Connection to server timed out after {timeout} seconds.")
        # Set all connections non-blocking after setup.
        for _, conn in self.conns.items():
            conn.setblocking(False)
        self.logger.info("\tAll channels connected!")

    def _close(self):
        """
        @brief Healthily closes the socket connection.

        Attempts to shutdown the socket, then closes.
        """
        for _, conn in self.conns.items():
            conn.shutdown(socket.SHUT_WR)
            conn.close()

    def _send_msg(self, channel:Comms.Channels, message:bytes):
        """
        @brief Sends a message to the server.
        
        This method uses selectors to wait for the channel socket to be ready 
        before sending SEND_BUFFER SIZE bytes at a time, making it non-blocking 
        compliant.

        @param channel The channel to send the message on (CTRL, SEND, or RECV).
        @param message The message to send to the server.
        @exception RuntimeError Raised if the socket connection is broken or 
                                if the socket is not ready to send.
        """
        # Wait to write.
        _, ready_to_send, _ = select.select([], [self.conns[channel]], [])
        if ready_to_send:
            # Send bytes, buffer by buffer if necessary.
            while len(message):
                bytes_sent = self.conns[channel].send(message, SEND_BUFFER_SIZE)
                if bytes_sent == 0:
                    raise RuntimeError("Socket connection broken, nothing sent.")
                message = message[bytes_sent:]
        else:
            self.logger.error("Socket not ready to send, cannot send message.")

    def _recv_msg(self, channel:Comms.Channels, timeout:int|None=None):
        """
        @brief Receives a message from the server.
        
        This method uses selectors to wait for the the channel socket to be 
        ready before receiving up to RECEIVE_BUFFER SIZE bytes at a time, making 
        it non-blocking compliant.

        @param channel The channel to send the message on (CTRL, SEND, or RECV).
        @param timeout Maximum time in seconds to wait for a response from the
                       server. If None, it will wait indefinitely.
        """
        # Wait to select or timeout.
        args = [[self.conns[channel]], [], []]
        if timeout is not None:
            args.append(timeout)
        ready_to_read, _, _ = select.select(*args)
        # Start reading in chunk by chunk.
        if len(ready_to_read) == 0:
            self.logger.error(f"Timed out after {timeout} seconds.")
            return None
        elif len(ready_to_read) > 0:
            data = b''
            while True:
                try:
                    chunk = self.conns[channel].recv(RECEIVE_BUFFER_SIZE)
                    data += chunk
                    # If chunk is less than the buffer size, we're done.
                    if len(chunk) < RECEIVE_BUFFER_SIZE:
                        return data
                    # Otherwise, poll to see if there's more waiting.
                    else:
                        _fd, _, _ = select.select([self.conns[channel]], [], [], 0)
                        if not len(_fd):
                            return data
                except socket.error as e:
                    print(f"Socket error: {e}")
                    return None
        return None
    
    async def _listen_msg(self, channel:Comms.Channels):
        """
        @brief Asyncronously listens for a message.
        
        This method is implemented similarly to the _recv_msg() method, however,
        it uses asynchronous socket receiving to wait for a message to come in 
        over the target channel without blocking other potential coroutines.

        @param channel The channel to send the message on (CTRL, SEND, or RECV).
        @param timeout Maximum time in seconds to wait for a response from the
                       server. If None, it will wait indefinitely.
        """
        data = b''
        while True:
            try:
                chunk = await asyncio.get_event_loop().sock_recv(self.conns[channel], RECEIVE_BUFFER_SIZE)
                data += chunk
                # If chunk is less than the buffer size, we're done.
                if len(chunk) < RECEIVE_BUFFER_SIZE:
                    return data
                # Otherwise, poll to see if there's more waiting.
                else:
                    _fd, _, _ = select.select([self.conns[channel]], [], [], 0)
                    if not len(_fd):
                        return data
            except socket.error as e:
                print(f"Socket error: {e}")
                return None
    
    def __del__(self):
        """
        @brief Closes connection to the server.

        @see _close()
        """
        self._close()
    

### DATABASE CONNECTORS ###

class DBConnector:
    """
    @brief Base class for database connectors.

    Database connectors enable NSB to use a database to store and retrieve 
    larger messages being sent over the network. To use this, the 
    _database.use_db_ parameter must be configured to be true. This class itself
    does not contain the necessary store(), check_out(), and peek() methods 
    necessary for NSB, but it can be inherited by other connector 
    implications that implement those methods. This class does provide a basic 
    message key generator.

    @see RedisConnector
    """
    def __init__(self, client_id:str):
        """
        @brief Base class constructor.
        
        Stores the client ID for the database connector and initiatializes a 
        payload counter and lock method call to be used in the payload ID 
        generator.

        @param client_id The identifier for the client object using this 
                         connector.

        @see generate_payload_id()
        """
        self.client_id = client_id
        self.plctr = 0
        self.lock = threading.Lock()
    def generate_payload_id(self):
        """
        @brief Payload ID generator.
        
        This simple message key generator method uses the client ID, the current 
        time, and an incrementing counter to create unique message IDs. This can 
        be overridden by child class methods.

        @returns str The newly generated unique ID for a payload.
        """
        with self.lock:
            self.plctr = (self.plctr + 1) & 0xFFFFF
            tms = int(time.time() * 1000) & 0x1FFFFFFFFFF
            payload_id = f"{tms}-{self.client_id}-{self.plctr}"
            return payload_id


class RedisConnector(DBConnector):
    """
    @brief Connector for Redis Database.

    This connector enables NSB to store and retrieve payload data using Redis's 
    in-memory key-value store. This class is built on the base database 
    connector class.

    @see DBConnector
    """
    def __init__(self, client_id:str, address:str, port:int):
        """
        @brief Constructor for RedisConnector.

        This constructor passes the client ID to the base class constructor and
        uses the address and port to connect to the Redis instance. The Redis 
        server must be started from outside this program.

        @param client_id The identifier of the client object that is using this
                         connector.
        @param address The address of the Redis server.
        @param port The port to be used to access the Redis server.
        """
        super().__init__(client_id)
        self.address = address
        self.port = port
        self.r = redis.Redis(host=self.address, port=self.port)

    def is_connected(self):
        """
        @brief Checks connection to the Redis server.

        This method pings the Redis server to check for connectivity. Because
        this method relies on a response from the server, it should be carefully 
        considered when implementing in systems where lower latency is desired.

        @returns bool Whether or not the Redis server is reachable.
        """
        try:
            return self.r.ping()
        except redis.ConnectionError:
            return False
        
    def store(self, value:bytes):
        """
        @brief Stores a payload.

        This method creates a new unique payload key and then stores the
        payload under that key.

        @param value The value (payload) to be stored.
        """
        key = self.generate_payload_id()
        self.r.set(key, value)
        return key

    def check_out(self, key:str):
        """
        @brief Checks out a payload.

        This method uses the passed in key to check out a payload, deleting the
        payload after it has been retrieved. This method is expected to be used 
        in the final retrieval in the lifecycle of a payload.

        @param key The key to retrieve the payload from the Redis server.

        @see NSBAppClient.receive()
        """
        value = self.r.get(key)
        self.r.delete(key)
        return value
    
    def peek(self, key:str):
        """
        @brief Peeks at the payload at the given key.

        This method uses the passed in key to retrieve a payload, similar to 
        the check_out() method, but without deleting the stored value. This is
        expected to be used when payloads are fetched before they must be 
        accessible again later in the payload's lifetime.

        @param key The key to retrieve the payload from the Redis server.

        @see NSBSimClient.fetch()
        """
        return self.r.get(key)

    def __del__(self):
        """
        @brief Closes the Redis connection.
        """
        if self.is_connected():
            self.r.close()

### NSB Client Base Class ###

class NSBClient:
    """
    @brief NSB client base class.
    
    This class serves as the base for the implemented 
    clients (AppClient and SimClient) will be built on. It provides basic 
    methods and shared operation methods.
    """

    def __init__(self, server_address:str, server_port:int):
        """
        @brief Base constructor for NSB Clients.

        Sets the communications module to the desired network interface 
        (currently SocketInterface) to connect to the daemon server.

        @param server_address The address of the NSB daemon.
        @param server_port The port of the NSB daemon.

        @see SocketInterface
        """
        self.comms = SocketInterface(server_address, server_port)
        # Origin indicator to mark outgoing messages.
        self.og_indicator = None

    def initialize(self):
        """
        @brief Initializes configuration with the server.

        This method sends an INIT message containing information about itself to 
        the server and gets a response containing configuration parameters.
        """
        if hasattr(self, '_id'):
            client_id = self._id
        else:
            raise RuntimeError("Client identifier (_id) not set. Base class without proper implementation called.")
        self.logger.info(f"Initializing {client_id} with server at {self.comms.server_addr}:{self.comms.server_port}...")
        # Create and populate an INIT message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.INIT
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        nsb_msg.intro.identifier = client_id
        nsb_msg.intro.address = self.comms.conns[Comms.Channels.CTRL].getsockname()[0]
        nsb_msg.intro.ch_CTRL = self.comms.conns[Comms.Channels.CTRL].getsockname()[1]
        nsb_msg.intro.ch_SEND = self.comms.conns[Comms.Channels.SEND].getsockname()[1]
        nsb_msg.intro.ch_RECV = self.comms.conns[Comms.Channels.RECV].getsockname()[1]
        self.logger.debug(f"Address: {nsb_msg.intro.address} | CTRL: {nsb_msg.intro.ch_CTRL} | " + \
                          f"SEND: {nsb_msg.intro.ch_SEND} | RECV: {nsb_msg.intro.ch_RECV}")
        # Send the message over the CTRL
        self.comms._send_msg(Comms.Channels.CTRL, nsb_msg.SerializeToString())
        self.logger.debug(f"Sent INIT! Waiting...")
        response = self.comms._recv_msg(Comms.Channels.CTRL, timeout=DAEMON_RESPONSE_TIMEOUT)
        if len(response):
                # Parse in message.
                nsb_resp = nsb_pb2.nsbm()
                nsb_resp.ParseFromString(response)
                # Check to see that message is of expected operation.
                if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.INIT:
                    # Get the configuration.
                    if nsb_resp.HasField('config'):
                        self.cfg = Config(nsb_resp)
                        self.logger.info(f"{self.cfg}")
                        # If database is specified, start it up.
                        if self.cfg.use_db:
                            self.db = RedisConnector(client_id, self.cfg.db_address, self.cfg.db_port)
                        return
        raise RuntimeError("Failed to initialize NSB client. No response from server or invalid response.")

    def ping(self, timeout:int=DAEMON_RESPONSE_TIMEOUT):
        """
        @brief Pings the server.

        This method sends a PING message to the server and awaits a response. It
        returns whether or not the server is reachable.
        
        @param timeout Maximum time to wait for a response from the server.
                       Default is set to DAEMON_RESPONSE_TIMEOUT.
        
        @returns bool True if the server is reachable and responds correctly, 
                      False otherwise.
        """
        # Create and populate a PING message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.PING
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Send the message and get response.
        self.comms._send_msg(Comms.Channels.CTRL, nsb_msg.SerializeToString())
        self.logger.info("PING: Pinged server.")
        response = self.comms._recv_msg(Comms.Channels.CTRL, timeout=timeout)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.PING:
                if nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.SUCCESS:
                    self.logger.info("PING: Server has pinged back!")
                    return True
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.FAILURE:
                    self.logger.info("PING: Server has some issue, but is reachable.")
                    return False
                else:
                    self.logger.info("PING: Unexpected behavior at server.")
                    return False
        return False
        
    def exit(self):
        """
        @brief Instructs server and self to exit and shutdown.

        This method sends an EXIT message to the server before deleting itself.
        """
        # Create and populate an EXIT message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.EXIT
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Send the message.
        self.comms._send_msg(Comms.Channels.CTRL, nsb_msg.SerializeToString())
        self.logger.info("EXIT: Sent command to server.")
        # End self.
        del self

    ### MACROS ###
    def msg_get_payload_obj(self, msg):
        return msg.msg_key if self.cfg.use_db else msg.payload
    
    def msg_set_payload_obj(self, payload_obj, msg): 
        if self.cfg.use_db:
            msg.msg_key = payload_obj
        else:
            msg.payload = payload_obj


### NSB Application Client ###

class NSBAppClient(NSBClient):
    """
    @brief NSB Application Client interface.
    
    This client provides the high-level NSB interface to send and receive 
    messages via NSB by communicating to the daemon.
    """
    def __init__(self, identifier:str, server_address:str, server_port:int):
        """
        @brief Constructs the NSB Application Client interface.

        This method uses the base NSBClient's constructor, which initializes a 
        network interface to connect and communicate with the NSB daemon. It 
        also an identifier that should correspond to the identifier used in the 
        NSB system.

        @param identifier The identifier for this NSB application client, which
                should correspond to the identifier in NSB and simulator.
        @param server_address The address of the NSB daemon.
        @param server_port The port of the NSB daemon.
        """
        self._id = identifier
        self.logger = logging.getLogger(f"{self._id} (app)")
        super().__init__(server_address, server_port)
        self.og_indicator = nsb_pb2.nsbm.Manifest.Originator.APP_CLIENT
        self.initialize()
        
    def send(self, dest_id:str, payload:bytes):
        """
        @brief Sends a payload to the specified destination via NSB.
        
        This method creates an NSB SEND message with the appropriate information 
        and payload and sends it to the daemon. It does not expect a response 
        from the daemon.

        @param dest_id The identifier of the destination NSB client.
        @param payload The payload to send to the destination.
        """
        # Create and populate a SEND message.
        nsb_msg = nsb_pb2.nsbm()
        # Manifest.
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.SEND
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.MESSAGE
        # Metadata.
        nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
        nsb_msg.metadata.src_id = self._id
        nsb_msg.metadata.dest_id = dest_id
        nsb_msg.metadata.payload_size = len(payload)
        if self.cfg.use_db:
            # If using databsae, store the payload and set the payload key.
            nsb_msg.msg_key = self.db.store(payload)
        else:
            # If not using database, attach the payload.
            nsb_msg.payload = payload
        # Send NSB message to daemon.
        self.comms._send_msg(Comms.Channels.SEND, nsb_msg.SerializeToString())
        self.logger.info("SEND: Sent message + payload to server.")

    def receive(self, dest_id:str|None=None, timeout:int|None=None):
        """
        @brief Receives a payload via NSB.

        The implementations of this function differ based on the system mode.
        
        *In __PULL__ mode:*
        If the destination is specified, it will receive a payload for that 
        destination. This method creates an NSB RECEIVE message with the 
        appropriate information and payload and sends it to the daemon. It will
        then get a response that either contains a MESSAGE code and 
        carries the retrieved payload or contains a NO_MESSAGE code. If a 
        message is found, the entire NSB message is returned to provide access
        to the metadata.

        *In __PUSH__ mode:*
        This method will await a message on the Comms.Channels.RECV channel 
        using _select_, with an optional timeout. If you want to achieve polling
        behavior, set _timeout_ to be 0. If this is being used to listen 
        indefinitely, set the timeout to be None. Listening indefinitely 
        will result in blocking behavior, but is recommended for asynchronous 
        listener implementations.

        @param dest_id The identifier of the destination NSB client. The default
                       None value will automatically assume the destination is 
                       self.
        @param timeout The amount of time in seconds to wait to receive data. 
                       None denotes waiting indefinitely while 0 denotes polling
                       behavior.

        @returns nsb_pb2.nsbm|None The NSB message containing the received 
                                   payload and metadata if a message is found, 
                                   otherwise None.

        @see Config.SystemMode
        @see SocketInterface._recv_msg()
        """
        if self.cfg.system_mode == Config.SystemMode.PULL:
            # Create and populate a FETCH message.
            nsb_msg = nsb_pb2.nsbm()
            # Manifest.
            nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.RECEIVE
            nsb_msg.manifest.og = self.og_indicator
            nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
            # Metadata.
            nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
            if dest_id:
                nsb_msg.metadata.dest_id = dest_id
            else:
                nsb_msg.metadata.dest_id = self._id
            # Send the NSB message + payload.
            self.comms._send_msg(Comms.Channels.RECV, nsb_msg.SerializeToString())
            self.logger.info("RECEIVE: Polling the server.")
        # Get response from request or just wait for message to come in.
        response = self.comms._recv_msg(Comms.Channels.RECV, timeout=timeout)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            # Check to see that message is of expected operation.
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.RECEIVE or nsb_pb2.nsbm.Manifest.Operation.FORWARD:
                # Check to see if there is a message at all.
                if nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.MESSAGE:
                    if self.cfg.use_db:
                        # If using a database, retrieve the payload.
                        payload = self.db.check_out(nsb_resp.msg_key)
                        nsb_resp.payload = payload
                    else:
                        payload = nsb_resp.payload
                    self.logger.info(f"RECEIVE: Received {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{payload}")
                    return nsb_resp
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE:
                    self.logger.info("RECEIVE: Yikes, no message.")
                    return None
        # If nothing, return None.
        return None
    
    async def listen(self):
        """
        @brief Asynchronously listens for a payload via NSB.

        This method returns a coroutine to be used in asynchronous calls. Its 
        implementation is based on the receive() method, but leverages the 
        asynchronous _listen_msg() instead.

        @returns nsb_pb2.nsbm|None The NSB message containing the received 
                                   payload and metadata if a message is found, 
                                   otherwise None.

        @see NSBAppClient.receive()
        @see SocketInterface._listen_msg()
        """
        # Get response from request or just wait for message to come in.
        response = await self.comms._listen_msg(Comms.Channels.RECV)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            # Check to see that message is of expected operation.
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.RECEIVE or nsb_pb2.nsbm.Manifest.Operation.FORWARD:
                # Check to see if there is a message at all.
                if nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.MESSAGE:
                    if self.cfg.use_db:
                        # If using a database, retrieve the payload.
                        payload = self.db.check_out(nsb_resp.msg_key)
                        nsb_resp.payload = payload
                    else:
                        payload = nsb_resp.payload
                    self.logger.info(f"RECEIVE: Received {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{payload}")
                    return nsb_resp
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE:
                    self.logger.info("RECEIVE: Yikes, no message.")
                    return None
        # If nothing, return None.
        return None

### NSB Simulator Client ###

class NSBSimClient(NSBClient):
    """
    @brief NSB Simulator Client interface.
    
    This client provides the high-level NSB interface to fetch and post messages 
    via NSB by communicating to the daemon.
    """
    def __init__(self, identifier:str, server_address:str, server_port:int):
        """
        @brief Constructs the NSB Simulator Client interface.

        This method uses the base NSBClient's constructor, which initializes a 
        network interface to connect and communicate with the NSB daemon.

        @param identifier The identifier for this NSB application client, which
               should correspond to the identifier in NSB and simulator.
        @param server_address The address of the NSB daemon.
        @param server_port The port of the NSB daemon.
        """
        self._id = identifier
        self.logger = logging.getLogger(f"{self._id} (sim)")
        super().__init__(server_address, server_port)
        self.og_indicator = nsb_pb2.nsbm.Manifest.Originator.SIM_CLIENT
        self.initialize()

    def fetch(self, src_id:str|None=None, timeout=None, get_payload=False):
        """
        @brief Fetches a payload that needs to be sent over the simulated 
               network.
        
        If the source is specified, it will try and fetch a payload for that 
        source. This method creates an NSB FETCH message with the appropriate 
        information and payload and sends it to the daemon. It will then get a 
        response that either contains a MESSAGE code and carries the fetched 
        payload or contains a NO_MESSAGE code. If a message is found, the entire 
        NSB message is returned to provide access to the metadata.

        @param src_id The identifier of the targe source. The default None value 
               will result in fetching the most recent message, regardless
               of source.
        @param timeout The amount of time in seconds to wait to receive data. 
               None denotes waiting indefinitely while 0 denotes polling 
               behavior.
        @param get_payload Whether or not to retrieve the payload. Setting this 
                           to True enables passing the actual payload through 
                           the simulated network. Setting this to False may 
                           result in lower latency for the system.

        @returns nsb_pb2.nsbm|None The NSB message containing the fetched 
                                   payload and metadata if a message is found, 
                                   otherwise None.
        """
        if self.cfg.system_mode == Config.SystemMode.PULL:
            # Create and populate a FETCH message.
            nsb_msg = nsb_pb2.nsbm()
            # Manifest.
            nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.FETCH
            nsb_msg.manifest.og = self.og_indicator
            nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
            # Metadata.
            if src_id:
                nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
                nsb_msg.metadata.src_id = src_id
            # Send the NSB message + payload.
            self.comms._send_msg(Comms.Channels.RECV, nsb_msg.SerializeToString())
            self.logger.info("FETCH: Sent fetch request to server.")
        # Get response from request or await forwarded message.
        response = self.comms._recv_msg(Comms.Channels.RECV, timeout=timeout)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.FETCH or \
                nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.FORWARD:
                if nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.MESSAGE:
                    if self.cfg.use_db:
                        # If using a database, retrieve the payload.
                        payload = self.db.peek(nsb_resp.msg_key)
                        if get_payload:
                            nsb_resp.payload = payload
                    else:
                        payload = nsb_resp.payload
                    self.logger.info(f"FETCH: Got {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{payload}")
                    return nsb_resp
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE:
                    print("FETCH: Yikes, no message.")
                    return None
            else:
                return None
        else:
            return None
        
    async def listen(self):
        """
        @brief Asynchronously listens for a payload that needs to be sent over 
               the simulated network.
        
        This method returns a coroutine to be used in asynchronous calls. Its 
        implementation is based on the fetch() method, but leverages the 
        asynchronous _listen_msg() instead.

        @param src_id The identifier of the targe source. The default None value 
               will result in fetching the most recent message, regardless
               of source.

        @returns nsb_pb2.nsbm|None The NSB message containing the fetched 
                                   payload and metadata if a message is found, 
                                   otherwise None.
        
        @see NSBSimClient.fetch()
        @see SocketInterface._listen_msg()
        """
        # Get response from request or await forwarded message.
        response = await self.comms._listen_msg(Comms.Channels.RECV)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.FETCH or \
                nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.FORWARD:
                if nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.MESSAGE:
                    if self.cfg.use_db:
                        # If using a database, retrieve the payload.
                        payload = self.db.peek(nsb_resp.msg_key)
                    else:
                        payload = nsb_resp.payload
                    self.logger.info(f"FETCH: Got {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{payload}")
                    return nsb_resp
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE:
                    print("FETCH: Yikes, no message.")
                    return None
            else:
                return None
        else:
            return None
        
    def post(self, src_id:str, dest_id:str, payload_obj:bytes, payload_size:int, success:bool=True):
        """
        @brief Posts a payload to the specified destination via NSB.
        
        This is intended to be used when a payload is finished being processed 
        (either successfully delivered or dropped) and the simulator client 
        needs to hand it off back to NSB. This method creates an NSB SEND 
        message with the appropriate information and payload and sends it to the 
        daemon.

        @param src_id The identifier of the source NSB client.
        @param dest_id The identifier of the destination NSB client.
        @param payload_obj The payload or payload ID to post to the destination.
        @param success Whether the post was successful or not. If False, 
                       it will set the OpCode to NO_MESSAGE.
        """
        # Create and populate a SEND message.
        nsb_msg = nsb_pb2.nsbm()
        # Manifest.
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.POST
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.MESSAGE if success else \
            nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE
        # Metadata.
        nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
        nsb_msg.metadata.src_id = src_id
        nsb_msg.metadata.dest_id = dest_id
        nsb_msg.metadata.payload_size = payload_size
        self.msg_set_payload_obj(payload_obj, nsb_msg)
        # Send the NSB message + payload.
        self.comms._send_msg(Comms.Channels.SEND, nsb_msg.SerializeToString())
        self.logger.info("POST: Posted message + payload to server.")