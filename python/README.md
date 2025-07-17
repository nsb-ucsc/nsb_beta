# NSB Client API in Python

## Adding the Client Module
_Installable Python package coming soon._

To access the client module, we recommend directly just copying the contents 
of this directory to your Python project and use it as a module within your 
project.

If you want to install the package in development mode and want it to be 
accessible to other project spaces, you can install it. From this directory:
```
pip install -e .
```
Then, add the full path to this module to your PYTHONPATH, and add that to your
_.zshrc_ or _.bashrc_ file.
```
echo 'export PYTHONPATH="${PYTHONPATH}:/.../nsb_beta/python"' >> ~/.zshrc
```

## Basic API Usage

To use the client API, import the module:
```
import nsb_client as nsb
```
_Full integration guide coming soon. Please refer to the Python-generated 
documentation in the meantime._

### NSB Application Client (`NSBAppClient`)

You can initialize a `NSBAppClient` using its constructor:
```
nsb_conn = nsb.NSBAppClient(identifier, server_address, server_port)
```
*Parameters:*
- `identifier` (`str`): A unique identifier for this NSB application client 
instance. This identifier must match the corresponding identifier used within 
the NSB system and simulator for proper coordination.
- `server_address` (`str`): The network address (IP address or hostname) where 
the NSB daemon is running.
- `server_port` (`int`): The port number on which the NSB daemon is listening 
for client connections.

Upon constructing the application client, this method will initialize with the 
NSB Daemon (which must be running at time of execution), connect to the database
if configured to do so, and identify itself within the NSB system. We recommend 
having clients persist throughout the duration of a simulation.

#### Sending Payloads: (`send`)