import socket
import select
import time
import nsb_pb2
import asyncio

# Set up logging.
import logging
logging.basicConfig(level=logging.DEBUG,
                    format='%(asctime)s.%(msecs)03d - %(levelname)s - %(message)s',
                    datefmt='%H:%M:%S',
                    handlers=[
                        logging.StreamHandler(),
                    ])

SERVER_CONNECTION_TIMEOUT = 10
DAEMON_RESPONSE_TIMEOUT = 5
RECEIVE_BUFFER_SIZE = 1
SEND_BUFFER_SIZE = 4096

class SocketInterface:
    def __init__(self, server_address, server_port):
        # Set connection information.
        self.server_addr = server_address
        self.server_port = server_port
        # Create logger.
        self.logger = logging.getLogger("NSBClient")
        # Connect.
        self._connect()

    def _configure(self):
        # Configure client.
        self.conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.conn.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

    def _connect(self, timeout=SERVER_CONNECTION_TIMEOUT):
        self.logger.info(f"Connecting to daemon@{self.server_addr}:{self.server_port}...")
        target_time = time.time() + timeout
        while time.time() < target_time:
            try:
                self.logger.debug("\tAttempting...")
                self._configure()
                self.conn.connect((self.server_addr, self.server_port))
                self.logger.info("\tConnected!")
                self.conn.setblocking(False)
                return
            except socket.error as e:
                # print(f"Socket error: {e}")
                time.sleep(1)
        raise TimeoutError(f"Connection to server timed out after {timeout} seconds.")

    def _close(self):
        self.conn.shutdown(socket.SHUT_WR)
        self.conn.close()

    def _send_msg(self, message):
        _, ready_to_send, _ = select.select([], [self.conn], [])
        if ready_to_send:
            while len(message):
                bytes_sent = self.conn.send(message, SEND_BUFFER_SIZE)
                if bytes_sent == 0:
                    raise RuntimeError("Socket connection broken, nothing sent.")
                message = message[bytes_sent:]
        # self.conn.sendall(message)

    def _recv_msg(self, timeout=None):
        args = [[self.conn], [], []]
        if timeout is not None:
            args.append(timeout)
        ready_to_read, _, _ = select.select(*args)
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
    
    # def _await_msg(self, timeout)
    
    def __del__(self):
        self._close()

### NSB Client Base Class ###

class NSBClient:
    def __init__(self, server_address, server_port):
        self.comms = SocketInterface(server_address, server_port)
        self.listener_waiting = False
        self.listener_task = None

    async def _listener(self):
        while self.listener_running:
            incoming_data = self.comms._recv_msg()
            if incoming_data:
                pass

    def _start_listener(self, timeout=None):
        self.listener_running = True
        self.listener_task = asyncio.create_task(self._listener())

    def _stop_listener(self):
        pass

    def ping(self):
        """
        Pings the server.
        """
        # Create and populate a new PING message.
        nsb_msg = nsb_pb2.nsbm()
        nsb_msg.manifest.op = nsb_pb2.nsbm.Manifest.Operation.PING
        nsb_msg.manifest.og = nsb_pb2.nsbm.Manifest.Originator.APP_CLIENT
        nsb_msg.manifest.code = nsb_pb2.nsbm.Manifest.OpCode.SUCCESS
        # Send the message and get response.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("PING: Pinged server.")
        response = self.comms._recv_msg(timeout=DAEMON_RESPONSE_TIMEOUT)
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
        else:
            return False
        
    def exit(self):
        # Create and populate a new message.
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
    def __init__(self, identifier, server_address, server_port):
        self._id = identifier
        super().__init__(server_address, server_port)
        # Create distinct logger.
        self.logger = logging.getLogger(f"{self._id} (NSBAppClient)")

    def send(self, dest_id, payload):
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
        nsb_msg.payload = payload
        # Send the NSB message + payload.
        self.comms._send_msg(nsb_msg.SerializeToString())
        self.logger.info("SEND: Sent message + payload to server.")

### NSB Simulator Client ###

class NSBSimClient(NSBClient):
    def __init__(self, server_address, server_port):
        super().__init__(server_address, server_port)
        # Create distinct logger.
        self.logger = logging.getLogger("NSBSimClient")
        

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

def test_send():
    app1 = NSBAppClient("billy", "127.0.0.1", 65432)
    app2 = NSBAppClient("bob", "127.0.0.1", 65432)
    app1.send("bob", b"hello world")
    time.sleep(2)
    app1.exit()

### MAIN FUNCTION (FOR TESTING) ###

if __name__ == "__main__":
    # test_ping()
    test_send()