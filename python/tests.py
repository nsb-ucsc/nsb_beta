from nsb_client import NSBAppClient, NSBSimClient, RedisConnector
import time
import threading
import asyncio
import random
import logging

"""
@file tests.py
@brief Suite of tests for Python NSB Client Library.

This file includes tests used to develop and iterate on NSB's client libraries 
for Python. In addition to being used for testing, they can also be used as 
implementation examples.
"""

### TEST FUNCTIONS ###

def test_pull_mode():
    """
    @brief Simple test for NSB in _PULL_ mode.

    This is a simple run of two Application Clients, one sending a message to 
    another, through a dummy Simulator Client via NSB. This test run is intended 
    for NSB in _PULL_ mode and will not work for NSB in _PUSH_ mode. Ensure that
    the system configuration reflects this.
    """
    # Create app and sim clients.
    app1 = NSBAppClient("billy", "127.0.0.1", 65432)
    app2 = NSBAppClient("bob", "127.0.0.1", 65432)
    sim = NSBSimClient("sim", "127.0.0.1", 65432)
    # Send a message.
    app1.send("bob", b"hello world")
    # Fetch a message at the sim client.
    fetched_msg = sim.fetch(0)
    # Post the message from the sim client.
    if fetched_msg:
        if fetched_msg.HasField('metadata'):
            sim.logger.info(f"Fetched payload@{fetched_msg.msg_key} " + \
                            f"from {fetched_msg.metadata.src_id} to {fetched_msg.metadata.dest_id}")
            sim.post(fetched_msg.metadata.src_id,
                     fetched_msg.metadata.dest_id,
                     fetched_msg.msg_key,
                     fetched_msg.metadata.payload_size,
                     success=True)
    else:
        print("No message fetched.")
    # Receive message.
    received_msg = app2.receive(0)
    if not received_msg:
        print("No message received.")
    app2.exit()

def test_push_mode():
    """
    @brief Simple test for NSB in _PUSH_ mode.

    This is a simple, threaded run of two Application Clients, one sending a 
    message to another, through a dummy Simulator Client via NSB. This test run 
    is intended for NSB in _PUSH_ mode and will not work for NSB in _PULL_ mode. 
    Ensure that the system configuration reflects this.

    The sending application client will run on the main thread, and the 
    simulator client and receiving application client each run on different 
    threads.
    """
    # Create app and sim clients.
    sim = NSBSimClient("sim", "127.0.0.1", 65432)
    app1 = NSBAppClient("app1", "127.0.0.1", 65432)
    app2 = NSBAppClient("app2", "127.0.0.1", 65432)
    # Define the simulator task function.
    def sim_fetch():
        fetched_msg = sim.fetch()
        if fetched_msg:
            payload_obj = fetched_msg.payload if fetched_msg.HasField("payload") else fetched_msg.msg_key
            sim.logger.info(f"Sim received message: {payload_obj}")
            sim.post(fetched_msg.metadata.src_id,
                     fetched_msg.metadata.dest_id,
                     payload_obj,
                     fetched_msg.metadata.payload_size,
                     success=True)
        else:
            sim.logger.info("Sim received no message.")
    # Start the simulator's fetch thread.
    sim_thread = threading.Thread(target=sim_fetch)
    sim_thread.start()
    # Define receiving app task function.
    def app2_receive():
        received_msg = app2.receive()
        if received_msg:
            payload_obj = received_msg.payload if received_msg.HasField("payload") else received_msg.msg_key
            app2.logger.info(f"App2 received message: {payload_obj}")
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
    print("\nTest completed.\n")
    # Clean up.
    app1.exit()

def test_db():
    """
    @brief Simple test for database usage with the RedisConnector.

    This test creates two connection endpoints that store, peek, and checkout 
    values from a Redis server. The server must be started separately.

    @see RedisConnector
    """
    conn1 = RedisConnector("billy", "127.0.0.1", 5050)
    conn2 = RedisConnector("bob", "127.0.0.1", 5050)
    key1 = conn1.store(b"hello world")
    key2 = conn2.store(b"hola mundo")
    key3 = conn1.store(b"bonjour le monde")
    print(key1 ,conn2.peek(key1))
    print(key1 ,conn1.check_out(key1))
    print(key2 ,conn1.peek(key2))
    print(key2 ,conn2.check_out(key2))
    print(key3 ,conn2.peek(key3))
    print(key3 ,conn1.check_out(key3))

class AppWithListener:
    """
    @brief A simple application with sending and listening coroutines.

    This simple application leverages the asynchronous NSBAppClient.listen() 
    method to spin off an asychronous listening coroutine while also having a 
    sending coroutine that uses the standard synchronous NSBAppClient.send() 
    method. This is to be used with SimWithListener.

    @see NSBAppClient
    @see SimWithListener
    @see test_async_push()
    """
    def __init__(self, name:str, address:str, port:int, contacts:list, rate:float=0.5):
        """
        @brief Constructor for AppWithListener.

        @param name The application identifier that will be used with NSB.
        @param address Address of the NSB daemon server.
        @param port Port of the NSB daemon server.
        @param contacts A list of contacts that can be sent messages.
        @param rate How likely it is for this application to send a message to
                    its contacts.
        """
        self.name = name
        self.logger = logging.getLogger(f"{self.name} (app)")
        self.rate = rate
        self.contacts = contacts
        self.nsb = NSBAppClient(name, address, port)
        # Set runtime flags.
        self.listening = False
        self.running = False
    async def listen(self):
        """@brief Listener coroutine that listens and processes incoming payloads."""
        self.listening = True
        while self.listening:
            received_msg = await self.nsb.listen()
            self.logger.info(f"RECV {received_msg.metadata.src_id}-->{received_msg.metadata.dest_id} " + \
                             f"({received_msg.metadata.payload_size} B) {received_msg.payload}")
    async def run(self):
        """@brief Sending coroutine that sends messages until this app is killed."""
        self.running = True
        while self.running:
            for contact in self.contacts:
                if random.random() < self.rate:
                    self.logger.info(f"Sending to {contact}...")
                    self.nsb.send(contact, b"Hello from " + self.name.encode())
                await asyncio.sleep(random.random() * 5)
    def __del__(self):
        """@brief Class deconstructor."""
        # Kill listener if necessary.
        self.running = False
        self.listening = False

class SimWithListener:
    """
    @brief A simple application with a fetch-and-post coroutine.

    This simple dummy simulation instance that leverages the asynchronous 
    NSBSimClient.listen() method to spin off an asychronous fetching-and-posting 
    coroutine using the asynchronous NSBSimClient.listen() and synchronous 
    NSBSimClient.post() methods. This is to be used with AppWithListener.

    @see NSBSimClient
    @see AppWithListener
    @see test_async_push()
    """
    def __init__(self, name, address, port):
        """
        @brief Constructor for SimWithListener.

        @param name The simulator identifier that will be used with NSB.
        @param address Address of the NSB daemon server.
        @param port Port of the NSB daemon server.
        """
        self.name = name
        self.logger = logging.getLogger(f"{self.name} (sim)")
        self.nsb = NSBSimClient(name, address, port)
        self.running = False
    async def listen(self):
        """@brief Listener coroutine that fetches and posts incoming payloads."""
        self.running = True
        while self.running:
            self.logger.info("Listening...")
            msg = await self.nsb.listen()
            if msg:
                self.nsb.post(msg.metadata.src_id,
                                msg.metadata.dest_id,
                                msg.msg_key if self.nsb.cfg.use_db else msg.payload,
                                msg.metadata.payload_size,
                                success=True)
    def __del__(self):
        """@brief Class deconstructor."""
        self.running = False
        
async def test_async_push(agent_roster: list):
    """
    @brief A test with multiple agents asynchronously sending and receiving 
    messages through a dummy simulator via NSB.

    This test will create the applications and simulator, and then spin off
    sending and receiving coroutines for all of them. A simulator coroutine for 
    fetching and posting messages will also be launched.

    @param agent_roster A list of application identifiers.
    """
    # Start sim.
    sim = SimWithListener("ghost", "127.0.0.1", 65432)
    # sim.nsb.initialize()
    sim_runtime = asyncio.create_task(sim.listen())
    agents = []
    agent_listen_runtimes = []
    agent_send_runtimes = []
    for i, agent_name in enumerate(agent_roster):
        agent = AppWithListener(agent_name, "127.0.0.1", 65432, agent_roster)
        agents.append(agent)
        listen_runtime = asyncio.create_task(agent.listen())
        agent_listen_runtimes.append(listen_runtime)
        send_runtime = asyncio.create_task(agent.run())
        agent_send_runtimes.append(send_runtime)

    # Catch kill signal.
    try:
        await asyncio.gather(*agent_listen_runtimes, *agent_send_runtimes, sim_runtime)
    except asyncio.CancelledError:
        pass
    finally:
        # Kill all tasks.
        sim_runtime.cancel()
        for runtime in agent_listen_runtimes:
            runtime.cancel()
        for runtime in agent_send_runtimes:
            runtime.cancel()
        # Cleanup.
        sim.__del__()
        for agent in agents:
            agent.__del__()
        print("Scenario completed. All agents cleaned up.")
    
    

### MAIN FUNCTION (FOR TESTING) ###

if __name__ == "__main__":
    # test_pull_mode()
    # test_push_mode()
    # test_db()
    asyncio.run(test_async_push(["agent1", "agent2", "agent3"]))