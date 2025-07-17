This README outlines usage of different examples that involve OMNeT++
and INET. Before doing any of these test simulations, make sure you have
OMNeT++ installed in your system.

### **Installation Steps for OMNeT++**

**Highly recommended** to install
[[OMNeT++6.1]](https://omnetpp.org/download/) . Please do
this, before running simulations from any of the four folders.

For mac OS silicon users, download the *aarch* version and refer to the
[[installation
document]](https://doc.omnetpp.org/omnetpp/InstallGuide.pdf)
to make sure to have all the pre-requisites installed, and the
environment correctly set up.

- Download the OMNeT version for your OS from
  [[here]](https://omnetpp.org/download/)

- Please follow the install instructions from
  [[here]](https://doc.omnetpp.org/omnetpp/InstallGuide.pdf)

- Create an OMNeT++ workspace under your omnetpp-6.1 folder
  ***\<your_workspace\>***

- From your terminal, in omnetpp-6.1 ; run the following commands, which
  will set the environment and bring up the OMNeT++ IDE :

```bash
source setenv
omnetpp
```

### **nsb_omnet_basic**

This folder contains all the necessary files to run a complete example
of using NSB with a pure OMNeT++ setup. You can find the following files :

***NSBMessage.msg** :* which is the custom OMNeT++ message that carries
NSB info. But in general, it will be for communication within OMNeT++ (
and with INET, this would have been the INET chunk )

***NSBHost.cc** :* A host file, that is capable of receiving messages
from NSBDaemon, and sending it to the correct simulated host and
notifying NSBDaemon of message delivery (along with associated
*NSBHost.h and NSBHost.ned )*

***NSBHostNetwork.ned*** : Creates a simple test network with two hosts,
of NSBHost type, to simulate ping between the two hosts.

**omnetpp.ini** : Configuration file to run the NSBHosNetwork.ned

### **Steps to run the OMNeT++ simulation**

1)  In order to run this example simulation, In your terminal, navigate
    to omnetpp6.1 and open your ***\<your_workspace\>*** from the IDE

    - ***File* -\> *Import* -\> *Existing project into workspace* :**
      select the ***nsb_omnet_basic*** project and add that to your
      workspace

2)  Under the makefrag file, found in this folder, make sure to have these lines included :
```bash
INCLUDE_PATH +=  $(shell pkg-config --cflags-only-I nsb)
LIBS +=  $(shell pkg-config --libs nsb)
```

3)  Now, select the omnetpp.ini and build the project through project-\>
    build project and then run this example. This simulates a 10host fully connected network. 
