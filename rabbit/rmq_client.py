import socket
import select
import time
import proto.nsb_pb2 as nsb_pb2


import pika
import pika.exceptions
import pika.adapters.asyncio_connection
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



"""
@file rmq_client.py
@namespace rmq_client
@brief RabbitMQ-based Application & Simulator Client Interfaces for NSB
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


class RabbitInterface(Comms):
    """
    @brief RabbitMQ-based interface for client-server communication.

    This class implements aocket interface network to facilitate network 
    communication between NSB clients and the server. This can be used as a 
    template to develop other interfaces for client communication, which must 
    define the same methods with the same arguments as done in this class.
    """
    def __init__(self, server_address: str, server_port: int):
        """
        @brief Constructor for the RabbitInterface class.

        Sets the address and port of the RabbitMQ broker before 
        connecting to the broker
        
        @param server_address The address of the RabbitMQ broker.
        @param server_port The port of the RabbitMQ broker.

        @see nsb_client.RabbitInterface._connect(timeout)
        """
        # Save connection information.
        self.server_addr = server_address 
        self.server_port = server_port
        # Create logger.
        self.logger = logging.getLogger(f"SIF({self.server_addr}:{self.server_port})")
        
        # RabbitMQ connection-specific stuff
        self._connection = None
        self._channels = {}  # One channel per Comms.Channels member
        self._queues = {}    # Mapping: Comms.Channels -> queue name
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
        self.logger.info(f"Connecting to broker@{self.server_addr}:{self.server_port}...")
        
        # Set target time for timing out.
        start_time = time.time()
        params = pika.ConnectionParameters(host=self.server_addr, port=self.server_port)

        # Establish a connection or timeout.
        while time.time() - start_time < timeout:
            try:
                self._connection = pika.BlockingConnection(params)
                for ch in Comms.Channels:
                    channel = self._connection.channel()
                    self._channels[ch] = channel

                    # Declare exchange once (on the first channel)
                    if ch == list(Comms.Channels)[0]:
                        channel.exchange_declare(exchange="main", exchange_type="direct")

                    qname = f"{ch.name}_rxq"
                    channel.queue_declare(queue=qname, exclusive=True)
                    channel.queue_bind(exchange="main", queue=qname, routing_key=qname)
                    self._queues[ch] = qname
                self.logger.info("Connected and channels established.")
                return
            except pika.exceptions.AMQPConnectionError:
                self.logger.debug("Retrying connection...")
                time.sleep(1)

        raise TimeoutError(f"RabbitMQ connection timed out after {timeout} seconds.")

    def _close(self):
        """
        @brief Healthily closes the RabbitMQ connection.

        Attempts to shutdown the channels, then closes connection if not open.
        """
        for ch in self._channels.values():
            if ch.is_open:
                ch.close()
        if self._connection and self._connection.is_open:
            self._connection.close()

    def _send_msg(self, channel:Comms.Channels, message:bytes):
        """
        @brief Sends a message to the server.
        
        This method tries to publish the message to the exchange, 
        routing it to the appropriate receiving queue as necessary.

        @param channel The channel to send the message on (CTRL, SEND, or RECV).
        @param message The message to send to the server.
        @exception RuntimeError Raised if the socket connection is broken or 
                                if the socket is not ready to send.
        """
        # Wait to write.
        try:
            self._channels[channel].basic_publish(
                exchange="main",
                routing_key=f"{channel.name}_rxq",
                body=message
            )
        except Exception as e:
            self.logger.error(f"Send failed on {channel.name}: {e}")
            raise RuntimeError("RabbitMQ send failed.")

    def _recv_msg(self, channel:Comms.Channels, timeout:int|None=None):
        """
        @brief Receives a message from the broker.
        
        This method receives a message from the queue, waiting upto timeout seconds
        before timing out.

        @param channel The channel to send the message on (CTRL, SEND, or RECV).
        @param timeout Maximum time in seconds to wait for a response from the
                       server. If None, it will wait indefinitely.
        """
        # Wait to select or timeout.
        qname = self._queues.get(channel)
        ch = self._channels.get(channel)
        if not ch or not qname:
            self.logger.error(f"No queue for channel {channel.name}")
            return None

        try:
            start_time = time.time()
            while True:
                method_frame, _, body = ch.basic_get(queue=qname, auto_ack=True)
                if method_frame:
                    return body
                if timeout and (time.time() - start_time) > timeout:
                    self.logger.error(f"Timeout on recv from {channel.name}")
                    return None
                time.sleep(0.1)
        except Exception as e:
            self.logger.error(f"Receive failed on {channel.name}: {e}")
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
        self.logger.error("Not implemented yet")
        return
    
    def __del__(self):
        """
        @brief Closes connection to the server.

        @see _close()
        """
        self._close()
    
class NSBRabbitClient:
    """
     @brief NSB RabbitMQ client base class.
    
    This class serves as the base for the implemented 
    clients (RabbitAppClient and RabbitSimClient) will be built on. It provides basic 
    methods and shared operation methods, similar to NSBClient but using RabbitInterface
    """
    
    def __init__(self, server_address: str, server_port: int):
        """
        @brief Constructor for RabbitMQ-backed NSB Clients.

        Sets up the RabbitInterface for communication with the NSB daemon.

        @param server_address The RabbitMQ broker address.
        @param server_port The RabbitMQ broker port.
        """
        self.comms = RabbitInterface(server_address, server_port)
        self.og_indicator = None  # Must be set in subclass (APP_CLIENT / SIM_CLIENT)
        
    def initialize(self):
        """
        @brief Initializes configuration with the server.
        """
        if not hasattr(self, '_id'):
            raise RuntimeError("Client identifier (_id) not set.")
        
        client_id = self._id
        self.logger.info(f"Initializing {client_id} with server at {self.comms.server_addr}:{self.comms.server_port}...")

        # Build INIT message
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.INIT
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        nsb_msg.intro.identifier = client_id
        nsb_msg.intro.address = self.comms.server_addr
        nsb_msg.intro.ch_CTRL = 0
        nsb_msg.intro.ch_SEND = 0
        nsb_msg.intro.ch_RECV = 0

        self.logger.debug(f"INIT message: {nsb_msg.intro}")

        # Send INIT and wait for response
        self.comms._send_msg(Comms.Channels.CTRL, nsb_msg.SerializeToString())
        self.logger.debug("Sent INIT. Waiting for response...")

        response = self.comms._recv_msg(Comms.Channels.CTRL, timeout=DAEMON_RESPONSE_TIMEOUT)
        if response:
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.INIT and nsb_resp.HasField("config"):
                self.cfg = Config(nsb_resp)
                self.logger.info(f"Configuration received:\n{self.cfg}")
                if self.cfg.use_db:
                    # TODO Implement Rabbit Redis connector, or just use existing one from nsb_client.py
                    self.db = RedisConnector(client_id, self.cfg.db_address, self.cfg.db_port)
                return
        raise RuntimeError("Failed to initialize NSB client: invalid or missing response.")
    
    def ping(self, timeout: int = DAEMON_RESPONSE_TIMEOUT):
        """
        @brief Pings the server to check connectivity.

        @returns True if successful, False otherwise.
        """
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.PING
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS

        self.comms._send_msg(Comms.Channels.CTRL, nsb_msg.SerializeToString())
        self.logger.info("PING: Sent to server.")

        response = self.comms._recv_msg(Comms.Channels.CTRL, timeout=timeout)
        if response:
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.PING:
                code = nsb_resp.manifest.code
                if code == nsb_pb2.nsbm.Manifest.OpCode.SUCCESS:
                    self.logger.info("PING: Server responded OK.")
                    return True
                elif code == nsb_pb2.nsbm.Manifest.OpCode.FAILURE:
                    self.logger.info("PING: Server is up, but returned FAILURE.")
                    return False
        self.logger.warning("PING: No response or unexpected response.")
        return False
    
    def exit(self):
        """
        @brief Sends an EXIT message to the server and closes the connection.
        """
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.EXIT
        nsb_msg.manifest.og = self.og_indicator
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS

        self.comms._send_msg(Comms.Channels.CTRL, nsb_msg.SerializeToString())
        self.logger.info("EXIT: Sent shutdown command to server.")
        self.comms._close()
        
    ### MACROS ###
    def msg_get_payload_obj(self, msg):
        return msg.msg_key if self.cfg.use_db else msg.payload

    def msg_set_payload_obj(self, payload_obj, msg):
        if self.cfg.use_db:
            msg.msg_key = payload_obj
        else:
            msg.payload = payload_obj

class NSBAppClientRMQ(NSBRabbitClient):
    """
    @brief NSB Application Client interface.
    
    This client provides the high-level NSB interface to send and receive 
    messages via NSB by communicating with the RabbitMQ broker.
    """
    def __init__(self, identifier:str, server_address:str, server_port:int):
        """
        @brief Constructs the NSB Application Client interface.

        This method uses the base NSBClient's constructor, which initializes a 
        RabbitMQ interface to connect and communicate with the RabbitMQ broker. It 
        also an identifier that should correspond to the identifier used in the 
        NSB system.

        @param identifier The identifier for this NSB application client, which
                should correspond to the identifier in NSB and simulator.
        @param server_address The address of the RabbitMQ broker.
        @param server_port The port of the RabbitMQ broker.
        """
        self._id = identifier
        self.logger = logging.getLogger(f"{self._id} (app-rabbit)")
        super().__init__(server_address, server_port)
        self.og_indicator = nsb_pb2.nsbm.Manifest.Originator.APP_CLIENT
        self.initialize()
        
    def send(self, dest_id:str, payload:bytes):
        """
        @brief Sends a payload to the specified destination via RabbitMQ broker.
        
        This method creates an NSB SEND message with the appropriate information 
        and payload and sends it to the daemon. It does not expect a response 
        from the simulator client nor broker.

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
        # Database stuff
        if self.cfg.use_db:
            nsb_msg.msg_key = self.db.store(payload)
        else:
            nsb_msg.payload = payload
        self.comms._send_msg(Comms.Channels.SEND, nsb_msg.SerializeToString())
        self.logger.info("SEND: Sent message + payload to server.")

    def receive(self, dest_id:str|None=None, timeout:int|None=None):
        """
        @brief Receives a payload via NSB.

        The implementations of this function differ based on the system mode.
        
        *In __PULL__ mode:*
        If the destination is specified, it will receive a payload for that 
        destination. This method creates an NSB RECEIVE message with the 
        appropriate information and payload and sends it to the broker. It will
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
        if response and len(response):
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