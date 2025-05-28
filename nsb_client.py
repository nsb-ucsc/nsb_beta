import socket
import select
import time
import nsb_pb2
import asyncio

# Set up logging.
import logging
logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s.%(msecs)03d\t%(name)s\t%(levelname)s\t%(message)s',
                    datefmt='%H:%M:%S',
                    handlers=[logging.StreamHandler(),])

SERVER_CONNECTION_TIMEOUT = 10
"""Maximum time a client will wait to connect to the daemon."""
DAEMON_RESPONSE_TIMEOUT = 600
"""Maximum time a client will wait to get a response from the daemon."""
RECEIVE_BUFFER_SIZE = 4096
"""Buffer size when receiving data."""
SEND_BUFFER_SIZE = 4096
"""Buffer size when sending data."""

class SocketInterface:
    """
    Socket interface network to facilitate network communication between NSB 
    clients and the server. This can be used as a template to develop other 
    interfaces for client communication, which must define the same methods with
    the same arguments as done in this class.
    """
    def __init__(self, server_address, server_port):
        """
        Sets the address and port of the server at the NSB daemon before 
        connecting to the server.

        Args:
            server_address (str): The address of the NSB daemon.
            server_port (int): The port of the NSB daemon.
        """
        # Save connection information.
        self.server_addr = server_address
        self.server_port = server_port
        # Create logger.
        self.logger = logging.getLogger("NSBClient")
        # Connect.
        self._connect()

    def _configure(self):
        """
        Configures the socket connection with appropriate options. This method 
        is called by the _connect(...) method.
        """
        # Create socket connection and configure for low latency and async.
        self.conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.conn.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

    def _connect(self, timeout=SERVER_CONNECTION_TIMEOUT):
        """
        Connects to the daemon with the stored server address and port. This 
        method uses the _configure() method to configure the socket and then 
        attempts to connect to the daemon.

        Args:
            timeout (int): Maximum time to wait for a connection to the daemon.
                Default is set to SERVER_CONNECTION_TIMEOUT.
        
        Raises:
            TimeoutError: If the connection to the server times out after the 
                specified timeout period.
        """
        self.logger.info(f"Connecting to daemon@{self.server_addr}:{self.server_port}...")
        # Set target time for timing out.
        target_time = time.time() + timeout
        while time.time() < target_time:
            # Try configuring and connecting to the daemon server.
            try:
                self.logger.debug("\tAttempting...")
                self._configure()
                self.conn.connect((self.server_addr, self.server_port))
                self.logger.info("\tConnected!")
                self.conn.setblocking(False)
                return
            # If the server isn't up or reachable, wait and try again.
            except socket.error as e:
                time.sleep(1)
        raise TimeoutError(f"Connection to server timed out after {timeout} seconds.")

    def _close(self):
        """
        Healthily closes the socket connection.
        """
        self.conn.shutdown(socket.SHUT_WR)
        self.conn.close()

    def _send_msg(self, message):
        """
        Sends a message to the server in a non-blocking-compliant way.

        Args:
            message (bytes): The message to send to the server.
        """
        # Wait to write.
        _, ready_to_send, _ = select.select([], [self.conn], [])
        if ready_to_send:
            # Send bytes, buffer by buffer if necessary.
            while len(message):
                bytes_sent = self.conn.send(message, SEND_BUFFER_SIZE)
                if bytes_sent == 0:
                    raise RuntimeError("Socket connection broken, nothing sent.")
                message = message[bytes_sent:]
        else:
            self.logger.error("Socket not ready to send, cannot send message.")

    def _recv_msg(self, timeout=None):
        """
        Receives a message from the server in a non-blocking-compliant way.

        Args:
            timeout (int): Maximum time to wait for a response from the server.
                If None, waits indefinitely. Default is None.
        """
        # Wait to select or timeout.
        args = [[self.conn], [], []]
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
                    chunk = self.conn.recv(RECEIVE_BUFFER_SIZE)
                    data += chunk
                    # If chunk is less than the buffer size, we're done.
                    if len(chunk) < RECEIVE_BUFFER_SIZE:
                        return data
                    # Otherwise, poll to see if there's more waiting.
                    else:
                        _fd, _, _ = select.select([self.conn], [], [], 0)
                        if not len(_fd):
                            return data
                except socket.error as e:
                    print(f"Socket error: {e}")
                    return None
        return None
    
    def __del__(self):
        """
        Processes the deletion of the object. Closes connection.
        """
        self._close()

### NSB Client Base Class ###

class NSBClient:
    """
    NSB client base class. This class serves as the base for the implemented 
    clients (AppClient and SimClient) will be built on. It provides basic 
    methods and shared operation methods.
    """
    def __init__(self, server_address, server_port):
        """
        Sets the communications module to the desired network interface 
        (currently SocketInterface) to connect to the daemon server.

        Args:
            server_address (str): The address of the NSB daemon.
            server_port (int): The port of the NSB daemon.
        """
        self.comms = SocketInterface(server_address, server_port)

    def ping(self, timeout=DAEMON_RESPONSE_TIMEOUT):
        """
        Pings the server and returns whether the the server is reachable or not.
        
        Args:
            timeout (int): Maximum time to wait for a response from the server.
                Default is set to DAEMON_RESPONSE_TIMEOUT.
        
        Returns:
            bool: True if the server is reachable and responds correctly, 
                  False otherwise.
        """
        # Create and populate a PING message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.PING
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.APP_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Send the message and get response.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("PING: Pinged server.")
        response = self.comms._recv_msg(timeout=timeout)
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
        Commands server to exit and shut down and shuts down self.
        """
        # Create and populate an EXIT message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.EXIT
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.APP_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Send the message.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("EXIT: Sent command to server.")
        # End self.
        del self


### NSB Application Client ###

class NSBAppClient(NSBClient):
    """
    NSB Application Client class. This client provides the high-level NSB 
    interface to send and receive messages via NSB by communicating to the 
    daemon.
    """
    def __init__(self, identifier, server_address, server_port):
        """
        Constructs self using base NSBClient's constructor, which initializes a 
        network interface to connect and communicate with the NSB daemon. Sets 
        an identifier that corresponds to the identifier used in the NSB system.

        Args:
            identifier (str): The identifier for this NSB application client, 
                should correspond to the identifier in NSB and simulator.
            server_address (str): The address of the NSB daemon.
            server_port (int): The port of the NSB daemon.
        """
        self._id = identifier
        self.logger = logging.getLogger(f"{self._id} (app)")
        super().__init__(server_address, server_port)

    def send(self, dest_id, payload):
        """
        Sends a payload to the specified destination via NSB. This method 
        creates an NSB SEND message with the appropriate information and payload 
        and sends it to the daemon.

        Args:
            dest_id (str): The identifier of the destination NSB client.
            payload (bytes): The payload to send to the destination.
        """
        # Create and populate a SEND message.
        nsb_msg = nsb_pb2.nsbm()
        # Manifest.
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.SEND
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.APP_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Metadata.
        nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
        nsb_msg.metadata.src_id = self._id
        nsb_msg.metadata.dest_id = dest_id
        nsb_msg.metadata.payload_size = len(payload)
        # Payload.
        nsb_msg.payload = payload
        # Send NSB message to daemon.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("SEND: Sent message + payload to server.")

    def receive(self, dest_id=None):
        """
        Receives a payload that has been addressed to the client. If the 
        destination is specified, it will receive a payload for that 
        destination. This method creates an NSB RECEIVE message with the 
        appropriate information and payload and sends it to the daemon. It will
        then get a response that either contains a MESSAGE code and 
        carries the retrieved payload or contains a NO_MESSAGE code. If a 
        message is found, the entire NSB message is returned to provide access
        to the metadata.

        Args:
            dest_id (str): The identifier of the destination NSB client.
            payload (bytes): The payload to send to the destination.

        Returns:
            nsb_pb2.nsbm | None: The NSB message containing the received payload 
                and metadata if a message is found, otherwise None.
        """
        # Create and populate a FETCH message.
        nsb_msg = nsb_pb2.nsbm()
        # Manifest.
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.RECEIVE
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.SIM_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Metadata.
        nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
        if dest_id:
            nsb_msg.metadata.dest_id = dest_id
        else:
            nsb_msg.metadata.dest_id = self._id
        # Send the NSB message + payload.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("RECEIVE: Polling the server.")
        # Get response.
        response = self.comms._recv_msg(timeout=DAEMON_RESPONSE_TIMEOUT)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            # Check to see that message is of expected operation.
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.RECEIVE:
                # Check to see if there is a message at all.
                if nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.MESSAGE:
                    self.logger.info(f"RECEIVE: Received {nsb_resp.metadata.payload_size} " + \
                                        f"bytes from {nsb_resp.metadata.src_id} to " + \
                                        f"{nsb_resp.metadata.dest_id}: " + \
                                        f"{nsb_resp.payload}")
                    return nsb_resp
                elif nsb_resp.manifest.code == nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE:
                    print("RECEIVE: Yikes, no message.")
                    return None
            else:
                return None
        else:
            return None

### NSB Simulator Client ###

class NSBSimClient(NSBClient):
    """
    NSB Simulator Client class. This client provides the high-level NSB 
    interface to fetch and post messages via NSB by communicating to the 
    daemon.
    """
    def __init__(self, server_address, server_port):
        """
        Constructs self using base NSBClient's constructor, which initializes a 
        network interface to connect and communicate with the NSB daemon.

        Args:
            server_address (str): The address of the NSB daemon.
            server_port (int): The port of the NSB daemon.
        """
        self.logger = logging.getLogger("(SimClient)")
        super().__init__(server_address, server_port)

    def fetch(self, src_id=None):
        """
        Fetches a payload that needs to be sent over the simulated network. If 
        the source is specified, it will try and fetch a payload for that 
        source. This method creates an NSB FETCH message with the appropriate 
        information and payload and sends it to the daemon. It will then get a 
        response that either contains a MESSAGE code and carries the fetched 
        payload or contains a NO_MESSAGE code. If a message is found, the entire 
        NSB message is returned to provide access to the metadata.

        Args:
            src_id (str): The identifier of the source NSB client.

        Returns:
            nsb_pb2.nsbm | None: The NSB message containing the fetched payload 
                and metadata if a message is found, otherwise None.
        """
        # Create and populate a FETCH message.
        nsb_msg = nsb_pb2.nsbm()
        # Manifest.
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.FETCH
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.SIM_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Metadata.
        if src_id:
            nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
            nsb_msg.metadata.src_id = src_id
        # Send the NSB message + payload.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("FETCH: Sent fetch request to server.")
        # Get response.
        response = self.comms._recv_msg(timeout=DAEMON_RESPONSE_TIMEOUT)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.nsbm.Manifest.Operation.FETCH:
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
        
    def post(self, src_id, dest_id, payload, success=True):
        """
        Posts a payload to the specified destination via NSB. This is intended 
        to be used when a payload is finished being processed (either 
        successfully delivered or dropped) and the simulator client needs to 
        hand it off back to NSB. This method creates an NSB SEND message with 
        the appropriate information and payload and sends it to the daemon.

        Args:
            src_id (str): The identifier of the source NSB client.
            dest_id (str): The identifier of the destination NSB client.
            payload (bytes): The payload to post to the destination.
            success (bool): Whether the post was successful or not. If False, 
                it will set the OpCode to NO_MESSAGE.
        """
        # Create and populate a SEND message.
        nsb_msg = nsb_pb2.nsbm()
        # Manifest.
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.POST
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.SIM_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.MESSAGE if success else \
            nsb_pb2.nsbm.Manifest.OpCode.NO_MESSAGE
        # Metadata.
        nsb_msg.metadata.addr_type = nsb_pb2.nsbm.Metadata.AddressType.STR
        nsb_msg.metadata.src_id = src_id
        nsb_msg.metadata.dest_id = dest_id
        nsb_msg.metadata.payload_size = len(payload)
        nsb_msg.payload = payload
        # Send the NSB message + payload.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("POST: Posted message + payload to server.")

### TEST FUNCTIONS ###

def dispatch_app_ping():
    app = NSBAppClient("billy", "127.0.0.1", 65432)
    return app.ping()
    
def test_persistent():
    app = NSBAppClient("billy", "127.0.0.1", 65432)
    app.test_send2(persistent=True)

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
    sim = NSBSimClient("127.0.0.1", 65432)
    app1.send("bob", b"hello world")
    time.sleep(2)
    fetched_msg = sim.fetch()
    time.sleep(2)
    if fetched_msg:
        if fetched_msg.HasField('metadata'):
            sim.post(fetched_msg.metadata.src_id,
                     fetched_msg.metadata.dest_id,
                     fetched_msg.payload,
                     success=True)
    else:
        print("No message fetched.")
    time.sleep(2)
    received_msg = app2.receive()
    time.sleep(2)
    if not fetched_msg:
        print("No message received.")
    app2.exit()

### MAIN FUNCTION (FOR TESTING) ###

if __name__ == "__main__":
    # test_ping()
    test_lifecycle()