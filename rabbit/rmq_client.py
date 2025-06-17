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
