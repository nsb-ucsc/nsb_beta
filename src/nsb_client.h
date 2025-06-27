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
        void msgSetPayloadObj(std::string payloadObj, nsb::nsbm msg);
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
        /**
         * @brief Receives a payload via NSB.
         * 
         * The implementations of this function differ based on the system mode.
         *
         * *In __PULL__ mode:*
         * If the destination is specified, it will receive a payload for that 
         * destination. This method creates an NSB RECEIVE message with the 
         * appropriate information and payload and sends it to the daemon. It will
         * then get a response that either contains a MESSAGE code and 
         * carries the retrieved payload or contains a NO_MESSAGE code. If a 
         * message is found, the entire NSB message is returned to provide access
         * to the metadata.
         * 
         * *In __PUSH__ mode:*
         * This method will await a message on the Comms.Channels.RECV channel 
         * using _select_, with an optional timeout. If you want to achieve polling
         * behavior, set _timeout_ to be 0. If this is being used to listen 
         * indefinitely, set the timeout to be None. Listening indefinitely 
         * will result in blocking behavior, but is recommended for asynchronous 
         * listener implementations.
         * 
         * @param destId The identifier of the destination NSB client. The default
         *               None value will automatically assume the destination is 
         *               self.
         * @param timeout The amount of time in seconds to wait to receive data. 
         *                None denotes waiting indefinitely while 0 denotes polling
         *                behavior.
         * 
         * @returns nsb.nsbm* The NSB message containing the received payload and 
         *                    metadata if a message is found, otherwise None.
         * 
         * @see Config.SystemMode
         * @see SocketInterface.receiveMessage()
         * 
         */
        nsb::nsbm* receive(std::string* destId, int timeout);
        nsb::nsbm* listenReceive();
    };

    class NSBSimClient : public NSBClient {
    public:
        NSBSimClient(const std::string& identifier, std::string& serverAddress, int serverPort);
        ~NSBSimClient();
        nsb::nsbm* fetch(std::string* srcId, int timeout, bool getPayload, std::string* payload);
        nsb::nsbm* listenFetch();
        void post(std::string srcId, std::string destId, std::string payloadObj, int payloadSize, bool success);
    };
}

#endif