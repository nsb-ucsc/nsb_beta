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


### **base_omnet_basic**

### **base_omnet_inet**

### **nsb_omnet_basic**

This folder contains all the necessary files to run a complete example
of using NSB with a pure OMNeT++ setup. Under the ***nsb_pure_omnet*** ,
you can find the following files :

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
      select the ***nsb_pure_omnet*** project and add that to your
      workspace

2)  Under the makefrag file, found in this folder, make sure to update
    the file path to where your nsb_beta lives.

3)  Now, select the omnetpp.ini and build the project through project-\>
    build project and then run this example !!!


### **nsb_omnet_inet**

This folder contains all the files necessary to test out INET
integration with NSB. There are two subfolders, namely : inet4.5 and
nsb_beta_simulations.

**Under inet4.5, is the NSB API integration.**

In this example, we use a UDP application for the backend networked
simulation. The specific NSB API calls can be found under
***inet/src/inet/applications/udpapp ;*** specifically in the files :
***nsbBasicApp*** and ***nsbSinkApp***

**nsbBasicApp** : Acts as the **message source**. Fetches messages from
the external NSB daemon and sends them into the simulation using UDP.

**nsbSinkApp** : Acts as the **message receiver**. Receives UDP packets
and notifies the NSB daemon of successful delivery*.*

**Under nsb_beta_simulations is the NED, ini files for testing
integration**

In particular, under the simulations folder, you can find two NED files,
one with 2hosts and another with 10hosts. Both of these can be called in
the omnetpp.ini file

### **Steps to run the INET simulation**

1.  In order to run this example simulation, In your terminal, navigate
    to omnetpp6.1 and open your ***\<your_workspace\>*** from the IDE

    a.  ***File* -\> *Import* -\> *Existing project into workspace* :**
        select the ***nsb_beta_simulations*** and ***inet4.5*** project
        and add that to your workspace

    b.  Right click on the ***nsb_beta_simulations*** folder, select
        **properties** -\> **Project References** and set inet4.5 as its
        project reference.

2.  Under the makefrag file, found under inet4.5/src, make sure to
    update the file path to where your nsb_beta lives.

3.  Now, select the omnetpp.ini found under nsb_beta_simulations and
    build the project through project-\> build project and then run this
    example !!!

### **Note**

Make sure to have your NSB Daemon running, before starting the IDE, and
also make sure to exit the IDE first, before killing the NSB Daemon for
a graceful exit.

