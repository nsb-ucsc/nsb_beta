// nsb_client.h

#ifndef NSB_CLIENT_H
#define NSB_CLIENT_H

#include "nsb.h"

namespace nsb {

    class NSBClient {
    public:
        NSBClient(const std::string& identifier, std::string serverAddress, int serverPort);
        ~NSBClient();
        void initialize();
        bool ping();
        void exit();
    protected:
        std::string msgGetPayloadObj(nsb::nsbm msg);
        void msgSetPayloadObj(std::string& payloadObj, nsb::nsbm msg);
        const std::string clientId;
        SocketInterface comms;
        nsb::nsbm::Manifest::Originator* originIndicator;
        Config cfg;
        RedisConnector* db;
    };

    class NSBAppClient : public NSBClient {
    public:
        NSBAppClient(const std::string& identifier, std::string& serverAddress, int serverPort);
        ~NSBAppClient();
        void send(const std::string& destId, std::string& payload, std::string* key);
        nsb::nsbm* receive(int* destId, int timeout);
        nsb::nsbm* listenReceive();
    };

    class NSBSimClient : public NSBClient {
    public:
        NSBSimClient(const std::string& identifier, std::string& serverAddress, int serverPort);
        ~NSBSimClient();
        nsb::nsbm* fetch(int* srcId, int timeout, bool getPayload);
        nsb::nsbm* listenFetch();
        void post(std::string srcId, std::string destId, int payloadSize, bool success);
    };
}

#endif