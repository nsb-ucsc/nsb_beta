/* SPDX-License-Identifier: GPL-2.0-only */

// Include statements and utility definitions...
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/aodv-module.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <sys/stat.h>
#include <ctime>


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NS3BasicSim");

struct NodeMapping {
    std::string ipString;
    std::string nodeIdString;
    ns3::Ipv4Address ipv4Addr;
    ns3::Ptr<ns3::Node> nodePtr;
    uint32_t wifiDeviceIndex;
    std::ofstream* logFile;
};

void CreateLogDirectory(const std::string& path, std::ostream* mainLog) {
    int status = mkdir(path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (mainLog) {
        if (status == 0) {
            *mainLog << Simulator::Now().GetSeconds() << "s [INFO] Created log directory '" << path << "'.\n";
        } else if (errno == EEXIST) {
            *mainLog << Simulator::Now().GetSeconds() << "s [INFO] Log directory '" << path << "' already exists.\n";
        } else {
            *mainLog << Simulator::Now().GetSeconds() << "s [WARN] Error creating log directory: " << strerror(errno) << "\n";
        }
    }
}

bool BuildNetworkTopology(const std::vector<NodeMapping>& nodeMappings,
                          NodeContainer& allSimNodes,
                          NetDeviceContainer& wifiDevices,
                          Ipv4InterfaceContainer& allInterfaces,
                          std::ostream& mainLog) {
    if (nodeMappings.empty()) return false;
    allSimNodes.Create(nodeMappings.size());

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(500.0));
    Ptr<YansWifiChannel> wifiChannel = channel.Create();

    YansWifiPhyHelper phy;
    phy.SetChannel(wifiChannel);

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("DsssRate1Mbps"),
                                 "ControlMode", StringValue("DsssRate1Mbps"));

    wifiDevices = wifi.Install(phy, mac, allSimNodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    int nodesPerRow = 10;
    double spacing = 50.0;
    for (size_t i = 0; i < nodeMappings.size(); ++i) {
        double x = (i % nodesPerRow) * spacing;
        double y = floor(i / nodesPerRow) * spacing;
        positionAlloc->Add(Vector(x, y, 0.0));
    }
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allSimNodes);

    InternetStackHelper stack;
    AodvHelper aodv;
    stack.SetRoutingHelper(aodv);
    stack.Install(allSimNodes);

    for (size_t i = 0; i < nodeMappings.size(); ++i) {
        Ptr<Ipv4> ipv4 = allSimNodes.Get(i)->GetObject<Ipv4>();
        uint32_t interfaceIndex = ipv4->AddInterface(wifiDevices.Get(i));
        Ipv4InterfaceAddress ipv4IfAddr(nodeMappings[i].ipv4Addr, Ipv4Mask("255.255.255.0"));
        ipv4->AddAddress(interfaceIndex, ipv4IfAddr);
        ipv4->SetMetric(interfaceIndex, 1);
        ipv4->SetUp(interfaceIndex);
        allInterfaces.Add(ipv4, interfaceIndex);
    }
    return true;
}

void ReceivePacket(Ptr<Socket> socket) {
    Address from;
    Ptr<Packet> packet = socket->RecvFrom(from);
    std::cout << "Received packet of size: " << packet->GetSize() << " bytes" << std::endl;
}

void SetupUdpReceiveSockets(const std::vector<NodeMapping>& nodeMappings, const NodeContainer& allSimNodes, std::ostream& mainLog) {
    for (size_t i = 0; i < nodeMappings.size(); ++i) {
        Ptr<Socket> recvSocket = Socket::CreateSocket(allSimNodes.Get(i), UdpSocketFactory::GetTypeId());
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 5000);
        recvSocket->Bind(local);
        recvSocket->SetRecvCallback(MakeCallback(&ReceivePacket));
    }
}

void SendGeneratedMessage(Ptr<Node> senderNode, Ipv4Address destAddr, std::string message) {
    Ptr<Socket> socket = Socket::CreateSocket(senderNode, UdpSocketFactory::GetTypeId());
    InetSocketAddress destination(destAddr, 5000);
    socket->Connect(destination);
    Ptr<Packet> packet = Create<Packet>((uint8_t*)message.c_str(), message.size());
    socket->Send(packet);
    socket->Close();
}

void GenerateTraffic(const std::vector<NodeMapping>& nodeMappings) {
    for (size_t i = 0; i + 1 < nodeMappings.size(); i += 2) {
        Simulator::Schedule(Seconds(2.0 + i), &SendGeneratedMessage,
                            nodeMappings[i].nodePtr,
                            nodeMappings[i + 1].ipv4Addr,
                            "Hello from " + nodeMappings[i].nodeIdString);

        Simulator::Schedule(Seconds(3.0 + i), &SendGeneratedMessage,
                            nodeMappings[i + 1].nodePtr,
                            nodeMappings[i].ipv4Addr,
                            "Reply from " + nodeMappings[i + 1].nodeIdString);
    }
}

int main(int argc, char* argv[]) {
    int numHosts = 10;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numHosts", "Number of simulated NS-3 hosts", numHosts);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    std::ofstream mainLogFile;
    CreateLogDirectory("log", nullptr);
    mainLogFile.open("log/main_simulation.log", std::ios_base::app);
    if (!mainLogFile.is_open()) {
        NS_LOG_ERROR("FATAL: Could not open main_simulation.log. Exiting.");
        std::cerr << "FATAL: Could not open log/main_simulation.log. Exiting." << std::endl;
        return 1;
    }

    std::vector<NodeMapping> nodeMappings;
    for (int i = 0; i < numHosts; ++i) {
        NodeMapping mapping;
        std::ostringstream ipStream;
        ipStream << "10.1.1." << (i + 1);
        mapping.ipString = ipStream.str();
        mapping.nodeIdString = "node" + std::to_string(i);
        mapping.logFile = nullptr;
        mapping.ipv4Addr = Ipv4Address(mapping.ipString.c_str());
        nodeMappings.push_back(mapping);
    }

    NodeContainer allSimNodes;
    NetDeviceContainer wifiDevices;
    Ipv4InterfaceContainer allInterfaces;

    if (!BuildNetworkTopology(nodeMappings, allSimNodes, wifiDevices, allInterfaces, mainLogFile)) {
        mainLogFile << Simulator::Now().GetSeconds() << "s [ERROR] Failed to build network topology.\n";
        mainLogFile.close();
        return 1;
    }

    for (size_t i = 0; i < nodeMappings.size(); ++i) {
        nodeMappings[i].nodePtr = allSimNodes.Get(i);
        nodeMappings[i].wifiDeviceIndex = i;
    }

    SetupUdpReceiveSockets(nodeMappings, allSimNodes, mainLogFile);
    GenerateTraffic(nodeMappings);

    Simulator::Stop(Seconds(20.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
