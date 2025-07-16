#Adapted from the tests.py file found under the python folder
import sys
import os

# Add the parent "python/" directory to sys.path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Now import the class
from nsb_client import NSBAppClient,  RedisConnector

#from nsb_client import 
import socket
import time


def test_push_mode():
    """
    @brief Simple test for NSB in _PULL_ mode.

    This is a simple run of two Application Clients, one sending a message to 
    another, through a dummy Simulator Client via NSB. This test run is intended 
    for NSB in _PULL_ mode and will not work for NSB in _PUSH_ mode. Ensure that
    the system configuration reflects this.
    """
    # Create app clients .
    app1 = NSBAppClient("host0", "127.0.0.1", 65432)
    app2 = NSBAppClient("host1", "127.0.0.1", 65432)
    #app3 = NSBAppClient("host2", "127.0.0.1", 65432)
    #app4 = NSBAppClient("host3", "127.0.0.1", 65432)

    # Start the message sending anf receiving sequence 
    app1.send("host1", b"hello world to host1")

    input("Press Enter to continue...")

    received_msg = app2.receive()

    input("Press Enter to continue...")

    app2.send("host0", b"hello second msg")

    input("Press Enter to continue...")

    received_msg = app1.receive()

    input("Press Enter to continue...")


    app1.send("host1", b"okay shall we end this host1?")      

    input("Press Enter to continue...")


    received_msg = app2.receive()     


    input("Press Enter to continue...")


    app2.send("host0", b"fine bye host0")

    input("Press Enter to continue...")

    received_msg = app1.receive()

    input("Please close simulation and save run to see stats, before clicking enter here, this will close the server too...")


    '''

    app3.send("host3", b"hello host3")

    input("Press Enter to continue...")

    received_msg = app4.receive("host3", None)

    input("Press Enter to continue...")

    app4.send("host2", b"Last msg of this exchange")

    input("Press Enter to continue...")

    received_msg = app3.receive("host2", None)

    input("Press Enter to continue...")

    app1.send("host1", b"hello world")

    input("Press Enter to continue...")

    input("Press Enter to continue...")

    received_msg = app2.receive("host1", None)

    input("Please close simulation and save run to see stats, before clicking enter here, this will close the server too...")

    '''
    '''

    app2.send("host0", b"hello again")

    input("Press Enter to continue...")

    input("Press Enter to continue...")

    received_msg = app1.receive("host0", None)

    input("Please close simulation and save run to see stats, before clicking enter here, this will close the server too...")
    '''


    #one of the apps initiates exit, for graceful shutdown of the NSBDaemon, but the simulator needs to close before this 
    app2.exit()




### MAIN FUNCTION (FOR TESTING) ###

if __name__ == "__main__":
     test_push_mode()
     

    
