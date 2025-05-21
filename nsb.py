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
RESPONSE_BUFFER_SIZE = 4096

### NSB Client Base Class ###

class NSBClient:
    def __init__(self, server_address, server_port):
        # Set connection information.
        self.server_addr = server_address
        self.server_port = server_port
        # Create logger.
        self.logger = logging.getLogger("NSBClient")
        # Connect.
        self.__connect()

    def __configure(self):
        # Configure client.
        self.conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # self.conn.setblocking(False)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.conn.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

    def __connect(self, timeout=SERVER_CONNECTION_TIMEOUT):
        self.logger.info(f"Connecting to daemon@{self.server_addr}:{self.server_port}...")
        target_time = time.time() + timeout
        while time.time() < target_time:
            try:
                self.logger.debug("\tAttempting...")
                self.__configure()
                self.conn.connect((self.server_addr, self.server_port))
                self.logger.info("\tConnected!")
                return
            except socket.error as e:
                # print(f"Socket error: {e}")
                time.sleep(1)
        raise TimeoutError(f"Connection to server timed out after {timeout} seconds.")

    def __close(self):
        self.conn.shutdown(socket.SHUT_WR)
        self.conn.close()

    def __send_message(self, message):
        self.conn.sendall(message)

    def __get_response(self, timeout=DAEMON_RESPONSE_TIMEOUT):
        self.conn.setblocking(False)
        # Set target time.
        target_time = time.time() + timeout
        data = b''
        message_exists = False
        while True and time.time() < target_time:
            data_arrived, _, _ = select.select([self.conn], [], [], 0)
            if data_arrived:
                try:
                    chunk = self.conn.recv(RESPONSE_BUFFER_SIZE)
                    if len(chunk):
                        message_exists = True
                        data += chunk
                    else:
                        break
                except socket.error as e:
                    print(f"Socket error: {e}")
                    self.conn.setblocking(True)
                    return None
            else:
                if message_exists:
                    self.conn.setblocking(True)
                    return data
        self.conn.setblocking(True)
        return None
    
    def __del__(self):
        self.__close()

### NSB Application Client ###

class NSBAppClient(NSBClient):
    def __init__(self, server_address, server_port):
        super().__init__(server_address, server_port)
        # Create distinct logger.
        self.logger = logging.getLogger("NSBAppClient")

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
        self._NSBClient__send_message(nsb_msg.SerializeToString())
        self.logger.info("PING: Pinged server.")
        response = self._NSBClient__get_response()
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
        self._NSBClient__send_message(nsb_msg.SerializeToString())
        self.logger.info("EXIT: Sent command to server.")
        # End self.
        del self
        

### TEST FUNCTIONS ###

def dispatch_app_ping():
    app = NSBAppClient("127.0.0.1", 65432)
    return app.ping()
    
def test_persistent():
    app = NSBAppClient("127.0.0.1", 65432)
    app.test_send2(persistent=True)

def test_ping():
    app = NSBAppClient("127.0.0.1", 65432)
    app.ping()
    app.exit()

### MAIN FUNCTION (FOR TESTING) ###

if __name__ == "__main__":
    import asyncio
    import random
    test_ping()