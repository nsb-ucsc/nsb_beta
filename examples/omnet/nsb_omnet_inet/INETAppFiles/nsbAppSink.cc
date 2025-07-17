//
// Copyright (C) 2000 Institut fuer Telematik, Universitaet Karlsruhe
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#include "nsbAppSink.h"

#include "inet/applications/base/ApplicationPacket_m.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/packet/Packet.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/transportlayer/contract/udp/UdpControlInfo_m.h"


#include "nsb_client.h" //new for supporting the nsb_socket development

#include<fstream>
namespace inet {

Define_Module(nsbAppSink);

nsbAppSink::~nsbAppSink()
{
    cancelAndDelete(selfMsg);
}

void nsbAppSink::initialize(int stage)
{
    ApplicationBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        numReceived = 0;
        bytesReceived =0; //new
        WATCH(numReceived);

        //adding parameters to support NSBSimClient interaction - TCP socket connection to the NSB daemon
        std::string myId = getParentModule()->getFullName(); //needs to be changed to reflect the appClient name 
        std::cout<<"This is the ID: "<<myId<<std::endl;
        std::string serverAddress = par("serverAddress").stringValue();
        int serverPort = par("serverPort");
        simClient = new nsb::NSBSimClient(myId, serverAddress, serverPort);


        localPort = par("localPort");
        startTime = par("startTime");
        stopTime = par("stopTime");
        if (stopTime >= SIMTIME_ZERO && stopTime < startTime)
            throw cRuntimeError("Invalid startTime/stopTime parameters");
        selfMsg = new cMessage("UDPSinkTimer");
//        connector = NSBConnector();
//        printf("Connector has been created.\n");
    }
}

void nsbAppSink::handleMessageWhenUp(cMessage *msg)
{
    if (msg->isSelfMessage()) {
        ASSERT(msg == selfMsg);
        switch (selfMsg->getKind()) {
            case START:
                processStart();
                break;

            case STOP:
                processStop();
                break;

            default:
                throw cRuntimeError("Invalid kind %d in self message", (int)selfMsg->getKind());
        }
    }
    else if (msg->arrivedOn("socketIn"))
        socket.processMessage(msg);
    else
        throw cRuntimeError("Unknown incoming gate: '%s'", msg->getArrivalGate()->getFullName());
}

void nsbAppSink::socketDataArrived(UdpSocket *socket, Packet *packet)
{
    // process incoming packet
    processPacket(packet);
}

void nsbAppSink::socketErrorArrived(UdpSocket *socket, Indication *indication)
{
    EV_WARN << "Ignoring UDP error report " << indication->getName() << endl;
    delete indication;
}

void nsbAppSink::socketClosed(UdpSocket *socket)
{
    if (operationalState == State::STOPPING_OPERATION)
        startActiveOperationExtraTimeOrFinish(par("stopOperationExtraTime"));
}

void nsbAppSink::refreshDisplay() const
{
    ApplicationBase::refreshDisplay();

    char buf[50];
    sprintf(buf, "rcvd: %d pks", numReceived);
    getDisplayString().setTagArg("t", 0, buf);
}

void nsbAppSink::finish()
{
    ApplicationBase::finish();
    EV_INFO << getFullPath() << ": received " << numReceived << " packets\n";
    std::cout << getFullPath() << ": received " << numReceived << " packets"<<std::endl; //new addition
    std::cout << getFullPath() << ": received " << bytesReceived << " bytes"<<std::endl; //new addition
    //std::cout<<"Total sum of bytes received in messages: "<< bytesReceived << std::endl; //new addition

    //add write to csv here

    std::string filename = "RcvdResults.csv";
    std::fstream file;
    //checking if file exists, if it doesnt add header
    if(!file){
        std::cout<<"Creating and writing header to csv file:"<<std::endl;
        file.open(filename, std::ios::out);
        file<<"Host Name"<< ","<< "Packets Received" << ","<<"Bytes Received" << std::endl;
        file.close();

    }
    if(file){
        std::cout<<"File exists so just append to it"<<std::endl;
        file.open(filename, std::ios::out | std::ios::app);
        file<<getFullPath()<<","<<numReceived <<"," <<bytesReceived <<","<<std::endl;
        file.close();
    }

}

void nsbAppSink::setSocketOptions()
{
    bool receiveBroadcast = par("receiveBroadcast");
    if (receiveBroadcast)
        socket.setBroadcast(true);

    MulticastGroupList mgl = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this)->collectMulticastGroups();
    socket.joinLocalMulticastGroups(mgl);

    // join multicastGroup
    const char *groupAddr = par("multicastGroup");
    multicastGroup = L3AddressResolver().resolve(groupAddr);
    if (!multicastGroup.isUnspecified()) {
        if (!multicastGroup.isMulticast())
            throw cRuntimeError("Wrong multicastGroup setting: not a multicast address: %s", groupAddr);
        socket.joinMulticastGroup(multicastGroup);
    }
    socket.setCallback(this);
}

void nsbAppSink::processStart()
{
    socket.setOutputGate(gate("socketOut"));
    socket.bind(localPort);
    setSocketOptions();

    if (stopTime >= SIMTIME_ZERO) {
        selfMsg->setKind(STOP);
        scheduleAt(stopTime, selfMsg);
    }
}

void nsbAppSink::processStop()
{
    if (!multicastGroup.isUnspecified())
        socket.leaveMulticastGroup(multicastGroup); // FIXME should be done by socket.close()
    socket.close();
}

void nsbAppSink::processPacket(Packet *pk)
{
    EV_INFO << "[RECV] Received packet: " << pk->getName()
            << ", total length: " << pk->getByteLength() << "\n";

    const auto& payload = pk->peekData<BytesChunk>();
    std::vector<uint8_t> vec = payload->getBytes();

    std::istringstream stream(std::string(vec.begin(), vec.end()));

    // Parse: [len1][srcId][len2][destId][payload]
    uint16_t lenSrc, lenDest;
    stream.read(reinterpret_cast<char*>(&lenSrc), sizeof(lenSrc));
    std::string srcId(lenSrc, '\0');
    stream.read(&srcId[0], lenSrc);

    stream.read(reinterpret_cast<char*>(&lenDest), sizeof(lenDest));
    std::string destId(lenDest, '\0');
    stream.read(&destId[0], lenDest);

    std::string payloadStr((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());

    EV_INFO << "[RECV] src = " << srcId << ", dest = " << destId
            << ", payload size = " << payloadStr.size() << "\n";

    // Post to NSB Daemon
    simClient->post(srcId, destId, payloadStr);

    delete pk;
    numReceived++;

}

void nsbAppSink::handleStartOperation(LifecycleOperation *operation)
{
    simtime_t start = std::max(startTime, simTime());
    if ((stopTime < SIMTIME_ZERO) || (start < stopTime) || (start == stopTime && startTime == stopTime)) {
        selfMsg->setKind(START);
        scheduleAt(start, selfMsg);
    }
}

void nsbAppSink::handleStopOperation(LifecycleOperation *operation)
{
    cancelEvent(selfMsg);
    if (!multicastGroup.isUnspecified())
        socket.leaveMulticastGroup(multicastGroup); // FIXME should be done by socket.close()
    socket.close();
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void nsbAppSink::handleCrashOperation(LifecycleOperation *operation)
{
    cancelEvent(selfMsg);
    if (operation->getRootModule() != getContainingNode(this)) { // closes socket when the application crashed only
        if (!multicastGroup.isUnspecified())
            socket.leaveMulticastGroup(multicastGroup); // FIXME should be done by socket.close()
        socket.destroy(); // TODO  in real operating systems, program crash detected by OS and OS closes sockets of crashed programs.
    }
}

} // namespace inet

