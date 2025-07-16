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

#ifndef __NSB_PURE_OMNET_NSBHOST_H_
#define __NSB_PURE_OMNET_NSBHOST_H_

#include <omnetpp.h>

#include "nsb_client.h"
#include "NSBMessage_m.h"

using namespace omnetpp;

class NSBHost : public cSimpleModule
{
  private:
    std::string hostId;
    nsb::NSBSimClient* simClient;
    cMessage* selfMsg = nullptr;
    simtime_t sendInterval;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage* msg) override;
    virtual void sendPacket();
    virtual void processPacket(NSBMessage* msg);

  public:
    virtual ~NSBHost();
};

#endif
