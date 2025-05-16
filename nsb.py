import socket

### NSB Application Client ###

class NSBAppClient:
    def __init__(self, server_address, server_port):
        self.conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_addr = server_address
        self.server_port = server_port

    def connect(self):
        # Connect to the server.
        self.conn.connect((self.server_addr, self.server_port))

    def send(self):
        self.conn.sendall(b"Hello World!")
    
    def __del__(self):
        # Close the connection.
        self.conn.close()

if __name__ == "__main__":
    app = NSBAppClient("127.0.0.1", 65432)
    input("Press Enter to connect...")
    app.connect()
    input("Press Enter to send...")
    app.send()
    input("Press Enter to end.")
    del app