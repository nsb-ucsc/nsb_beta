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