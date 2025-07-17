//
// Copyright (C) 2004 OpenSim Ltd.
// Copyright (C) 2000 Institut fuer Telematik, Universitaet Karlsruhe
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#ifndef __INET_NSBAPPSINK_H
#define __INET_NSBAPPSINK_H

#include "inet/applications/base/ApplicationBase.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "nsb_client.h" //new for supporting the nsb_socket development

namespace inet {

/**
 * Consumes and prints packets received from the Udp module. See NED for more info.
 */
class INET_API nsbAppSink : public ApplicationBase, public UdpSocket::ICallback
{
  protected:
    enum SelfMsgKinds { START = 1, STOP };

    UdpSocket socket;
    int localPort = -1;
    L3Address multicastGroup;
    simtime_t startTime;
    simtime_t stopTime;
    cMessage *selfMsg = nullptr;
    int numReceived = 0;
    
    //Creating NSBSimClient object
    nsb::NSBSimClient* simClient; 

    //new addition
    long bytesReceived = 0;

  public:
    nsbAppSink() {}
    virtual ~nsbAppSink();

  protected:
    virtual void processPacket(Packet *msg);
    virtual void setSocketOptions();

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(cMessage *msg) override;
    virtual void finish() override;
    virtual void refreshDisplay() const override;

    virtual void socketDataArrived(UdpSocket *socket, Packet *packet) override;
    virtual void socketErrorArrived(UdpSocket *socket, Indication *indication) override;
    virtual void socketClosed(UdpSocket *socket) override;

    virtual void processStart();
    virtual void processStop();

    virtual void handleStartOperation(LifecycleOperation *operation) override;
    virtual void handleStopOperation(LifecycleOperation *operation) override;
    virtual void handleCrashOperation(LifecycleOperation *operation) override;
};

} // namespace inet

#endif

