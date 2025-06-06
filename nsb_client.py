import socket
import select
import time
import nsb_pb2 as nsb_pb2
import asyncio

# Set up logging.
import logging
from enum import IntEnum
import threading

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

    def __repr__(self):
        """
        @brief String representation of the configuration.
        """
        s = f"[CONFIG] System Mode: {self.system_mode.name} | Use DB? {self.use_db}"
        if self.use_db:
            s += f" | DB Address: {self.db_address} | DB Port: {self.db_port}"
        return s

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
        @brief Constructor for the NSBClient class.

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
                    conn.setblocking(False)
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
        @brief Sends a message to the server.
        
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
    
    def __del__(self):
        """
        @brief Closes connection to the server.

        @see _close()
        """
        self._close()

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
        self.logger.info(f"Initializing {self.__getattribute__("_id")} with server at {self.comms.server_addr}:{self.comms.server_port}...")
        # Create and populate an INIT message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.INIT
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        nsb_msg.intro.identifier = self.__getattribute__("_id")
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
        # Payload.
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
                    self.logger.info(f"RECEIVE: Received {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{nsb_resp.payload}")
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

    def fetch(self, src_id:str|None=None, timeout=None):
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
                    self.logger.info(f"FETCH: Got {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{nsb_resp.payload}")
                    return nsb_resp
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE:
                    print("FETCH: Yikes, no message.")
                    return None
            else:
                return None
        else:
            return None
        
    def post(self, src_id:str, dest_id:str, payload:bytes, success:bool=True):
        """
        @brief Posts a payload to the specified destination via NSB.
        
        This is intended to be used when a payload is finished being processed 
        (either successfully delivered or dropped) and the simulator client 
        needs to hand it off back to NSB. This method creates an NSB SEND 
        message with the appropriate information and payload and sends it to the 
        daemon.

        @param src_id The identifier of the source NSB client.
        @param dest_id The identifier of the destination NSB client.
        @param payload The payload to post to the destination.
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
        nsb_msg.metadata.payload_size = len(payload)
        nsb_msg.payload = payload
        # Send the NSB message + payload.
        self.comms._send_msg(Comms.Channels.SEND, nsb_msg.SerializeToString())
        self.logger.info("POST: Posted message + payload to server.")

### TEST FUNCTIONS ###

def test_ping():
    app1 = NSBAppClient("billy", "127.0.0.1", 65432)
    app2 = NSBAppClient("bob", "127.0.0.1", 65432)
    app1.ping()
    time.sleep(2)
    app2.ping()
    time.sleep(2)
    app1.exit()

def test_lifecycle():
    app1 = NSBAppClient("billy", "127.0.0.1", 65432)
    app2 = NSBAppClient("bob", "127.0.0.1", 65432)
    sim = NSBSimClient("sim", "127.0.0.1", 65432)
    app1.initialize()
    app2.initialize()
    sim.initialize()
    app1.send("bob", b"hello world")
    time.sleep(2)
    fetched_msg = sim.fetch(0)
    time.sleep(2)
    if fetched_msg:
        if fetched_msg.HasField('metadata'):
            sim.logger.info(f"Fetched message: {fetched_msg.payload} " + \
                            f"from {fetched_msg.metadata.src_id} to {fetched_msg.metadata.dest_id}")
            sim.post(fetched_msg.metadata.src_id,
                     fetched_msg.metadata.dest_id,
                     fetched_msg.payload,
                     success=True)
    else:
        print("No message fetched.")
    time.sleep(2)
    received_msg = app2.receive(0)
    time.sleep(2)
    if not fetched_msg:
        print("No message received.")
    app2.exit()

def test_push_mode():
    sim = NSBSimClient("sim", "127.0.0.1", 65432)
    app1 = NSBAppClient("app1", "127.0.0.1", 65432)
    app2 = NSBAppClient("app2", "127.0.0.1", 65432)
    sim.initialize()
    app1.initialize()
    app2.initialize()

    def sim_fetch():
        fetched_msg = sim.fetch()
        if fetched_msg:
            sim.logger.info(f"Sim received message: {fetched_msg.payload}")
            sim.post(fetched_msg.metadata.src_id,
                     fetched_msg.metadata.dest_id,
                     fetched_msg.payload,
                     success=True)
        else:
            sim.logger.info("Sim received no message.")

    # Start the simulator's fetch thread.
    sim_thread = threading.Thread(target=sim_fetch)
    sim_thread.start()

    def app2_receive():
        received_msg = app2.receive()
        if received_msg:
            app2.logger.info(f"App2 received message: {received_msg.payload}")
        else:
            app2.logger.info("Nada.")
    
    # Start the receiving app's receive thread.
    app2_thread = threading.Thread(target=app2_receive)
    app2_thread.start()

    # Give the simulator and receiving app some time to start listening.
    time.sleep(1)

    # Send a message from the app.
    app1.send("app2", b"Hello from app!")

    # Wait for the simulator to process the message.
    sim_thread.join()
    app2_thread.join()

    print("\nGRÆÇIAS A DIOŠ\n")

    # Clean up.
    app1.exit()

### MAIN FUNCTION (FOR TESTING) ###

if __name__ == "__main__":
    # test_ping()
    # test_lifecycle()
    test_push_mode()