//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "NSBHost.h"

Define_Module(NSBHost);

void NSBHost::initialize()
{
    hostId = par("hostId").stdstringValue();
        std::string serverAddress = par("serverAddress").stdstringValue();
        int serverPort = par("serverPort");

        simClient = new nsb::NSBSimClient(hostId, serverAddress, serverPort);

        sendInterval = par("sendInterval");
        selfMsg = new cMessage("sendTimer");

        scheduleAt(simTime() + sendInterval, selfMsg);
}

void NSBHost::handleMessage(cMessage *msg)
{
    if (msg == selfMsg) {
            sendPacket();
            scheduleAt(simTime() + sendInterval, selfMsg);
        } else {
            auto nsbMsg = check_and_cast<NSBMessage*>(msg);
            processPacket(nsbMsg);
        }
}

void NSBHost::sendPacket() {
    //int timeout = 20;
    //nsb::MessageEntry entry = simClient->fetch(nullptr, timeout);
    nsb::MessageEntry entry = simClient->fetch();
    if (!entry.exists()) {
        EV_WARN << "[" << hostId << "] No message fetched\n";
        return;
    }

    EV_INFO << "[" << hostId << "] Fetched message to " << entry.destination << "\n";

    auto msg = new NSBMessage("ForwardedMsg");
    msg->setSrcId(entry.source.c_str());
    msg->setDestId(entry.destination.c_str());
    msg->setPayload(entry.payload_obj.c_str());

    //send(msg, "out");

    // Extract numeric index from "hostX"
    std::string dest = entry.destination.c_str();
    int destIndex = std::stoi(dest.substr(4));  // "host3" → 3
    std::cout<<"The gateid is:"<<destIndex<<std::endl;
    // Send via the correct gate index
    send(msg, "out", destIndex);

}

void NSBHost::processPacket(NSBMessage* msg) {
    EV_INFO << "[" << hostId << "] Received from " << msg->getSrcId()
            << " to " << msg->getDestId() << "\n";

    std::string dest = msg->getDestId();  // extract destination early
    int destIndex = std::stoi(dest.substr(4));  // "host3" → 3
    std::cout << "The gateid is: " << destIndex << std::endl;

    if (msg->getDestId() == hostId) {
        std::string payload = msg->getPayload();
        EV_INFO << "[" << hostId << "] I am the destination. Posting to daemon.\n";
        simClient->post(msg->getSrcId(), msg->getDestId(), payload);
        delete msg;
    } else {
        EV_INFO << "[" << hostId << "] Forwarding to " << msg->getDestId() << "\n";

        //send(msg, "out");
        send(msg, "out", destIndex);
    }
}

NSBHost::~NSBHost() {
    cancelAndDelete(selfMsg);
    delete simClient;
}
