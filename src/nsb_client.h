// nsb_client.h

#ifndef NSB_CLIENT_H
#define NSB_CLIENT_H

#include "nsb.h"

namespace nsb {

    int SERVER_CONNECTION_TIMEOUT = 10;
    int DAEMON_RESPONSE_TIMEOUT = 600;
    size_t RECEIVE_BUFFER_SIZE = 4096;
    int SEND_BUFFER_SIZE = 4096;

    class Comms {
    public:
        enum class Channel {
            CTRL = 0,
            SEND = 1,
            RECV = 2
        };
        const std::vector<Channel> Channels = {Channel::CTRL, Channel::SEND, Channel::RECV};
        std::string getChannelName(Channel channel) {
            return ChannelName.at(channel);
        }
    private:
        const std::map<Channel, std::string> ChannelName = {
            {Channel::CTRL, "CTRL"},
            {Channel::SEND, "SEND"},
            {Channel::RECV, "RECV"}
        };
    };

    class SocketInterface : public Comms {
    public:
        SocketInterface(const std::string& server_address, int server_port);
        ~SocketInterface();
        int connectToServer(int timeout);
        void closeConnection();
        int sendMessage(Comms::Channel channel, const std::string& message);
        std::string receiveMessage(Comms::Channel channel, int* timeout);
        std::future<std::string> listenForMessage(Comms::Channel channel, int* timeout);
    private:
        std::string serverAddress;
        int serverPort;
        std::map<Channel, int> conns;
    };

    


}

#endif