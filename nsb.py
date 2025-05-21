import socket
import select
import time
import nsb_pb2

DAEMON_RESPONSE_TIMEOUT = 5
RESPONSE_BUFFER_SIZE = 4096

### NSB Application Client ###

class NSBAppClient:
    def __init__(self, server_address, server_port):
        # Configure client.
        self.conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # self.conn.setblocking(False)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # self.conn.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        # Set connection information.
        self.server_addr = server_address
        self.server_port = server_port

    def __connect(self):
        self.conn.connect((self.server_addr, self.server_port))

    def __close(self):
        self.conn.shutdown(socket.SHUT_WR)
        self.conn.close()

    def __send_message(self, message):
        # Connect to the server and send message.
        self.__connect()
        self.conn.sendall(message)
        self.__close()

    def __get_response(self, timeout):
        self.conn.connect((self.server_addr, self.server_port))
        data = b''
        while True:
            data_arrived, _, _ = select.select([self.conn], [], [], timeout)
            if data_arrived:
                try:
                    chunk = self.conn.recv(RESPONSE_BUFFER_SIZE)
                    if len(chunk):
                        data += chunk
                    else:
                        break
                except socket.error as e:
                    self.conn.close()
                    raise
        self.conn.close()
        return data

    def ping(self):
        """
        Pings the server.
        """
        # Create and populate a new message.
        nsb_msg = nsb_pb2.Manifest()
        nsb_msg.op = nsb_pb2.Manifest.Operation.PING
        nsb_msg.og = nsb_pb2.Manifest.Originator.APP_CLIENT
        nsb_msg.code = nsb_pb2.Manifest.OpCode.SUCCESS
        # Send the message and get response.
        self.__send_message(nsb_msg.SerializeToString())
        response = self.__get_response(DAEMON_RESPONSE_TIMEOUT)
        if len(response):
            # Parse in message.
            nsb_resp = nsb_pb2.nsbm()
            nsb_resp.ParseFromString(response)
            if nsb_resp.manifest.op == nsb_pb2.Manifest.Operation.PING:
                if nsb_resp.manifest.code == nsb_pb2.Manifest.OpCode.SUCCESS:
                    print("PING: Server has pinged back!")
                    return True
                elif nsb_resp.manifest.code == nsb_pb2.Manifest.OpCode.FAILURE:
                    print("PING: Server has some issue, but is reachable.")
                    return False
                else:
                    print("PING: Unexpected behavior at server.")
                    return False
        else:
            return False

    def test_send1(self, message):
        self.__send_message(message)

    def test_send2(self, persistent=False):
        if persistent:
            self.__connect()
        while True:
            message = input(">> ")
            if message == "exit":
                break
            else:
                if persistent:
                    self.conn.sendall(message.encode())
                else:
                    self.__send_message(message.encode())
        if persistent:
            self.__close()

# Functions used for testing.
async def dispatch_app_client(role="ping"):
    app = NSBAppClient("127.0.0.1", 65432)
    input("Press Enter to continue...")
    app.test_send(b"hello")
    input("Press Enter to continue...")
    app.test_send(b"yurrt")
    input("Press Enter to continue...")
    app.test_send(b"skrrraa")
    input("Press Enter to continue...")
    del app

def dispatch_app_ping():
    app = NSBAppClient("127.0.0.1", 65432)
    return app.ping()

async def test():
    await asyncio.gather(dispatch_app_client(),
                         dispatch_app_client())
    
def test_persistent():
    app = NSBAppClient("127.0.0.1", 65432)
    app.test_send2(persistent=True)
    
if __name__ == "__main__":
    import asyncio
    import random
    test_persistent()