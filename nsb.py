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

# Function used for testing.
async def test():
    async def dispatch_app_client():
        app = NSBAppClient("127.0.0.1", 65432)
        await asyncio.sleep(random.randint(1,4))
        app.connect()
        await asyncio.sleep(random.randint(1,4))
        app.send()
        await asyncio.sleep(random.randint(1,4))
        del app
    await asyncio.gather(dispatch_app_client(),
                         dispatch_app_client())
    
if __name__ == "__main__":
    import asyncio
    import random
    asyncio.run(test())