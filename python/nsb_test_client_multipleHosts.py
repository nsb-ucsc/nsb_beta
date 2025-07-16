import sys
import os
import random
import string
import time

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from nsb_client import NSBAppClient

def random_string(length=8):
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def main():
    num_apps = 10
    apps = []

    # Create 10 clients: app1 to app10, each mapped to host0 to host9
    for i in range(num_apps):
        host_id = f"host{i}"
        client_id = f"app{i + 1}"  # app1 through app10
        client = NSBAppClient(host_id, "127.0.0.1", 65432)
        apps.append((client_id, host_id, client))
        print(f"Initialized {client_id} (maps to {host_id})")

    # Simulate 10 random send operations
    for _ in range(3):
        sender_index = random.randint(0, num_apps - 1)
        receiver_index = random.randint(0, num_apps - 1)
        while receiver_index == sender_index:
            receiver_index = random.randint(0, num_apps - 1)

        sender_name, sender_host, sender_client = apps[sender_index]
        receiver_name, receiver_host, receiver_client = apps[receiver_index]

        payload = f"Hi from {sender_name} to {receiver_name}: {random_string(6)}"
        #binary_payload = payload.encode('utf-8')
        print(f"[SEND] {sender_name} ({sender_host}) → {receiver_name} ({receiver_host}): {payload}")
        sender_client.send(receiver_host, payload)


    # Wait before processing receives
    wait_seconds = 90
    print(f"\n[WAIT] Waiting {wait_seconds} seconds before processing receives...\n")
    time.sleep(wait_seconds)

    # Now process receives
    for _, receiver_host, receiver_client in apps:
        print(f"[RECV] {receiver_host} checking inbox...")
        received = receiver_client.receive()
        print(f"[RECV] Received: {received}")

if __name__ == "__main__":
    main()
