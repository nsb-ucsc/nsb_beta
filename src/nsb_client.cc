// nsb.cc

#include "nsb_client.h"

namespace nsb {
    SocketInterface::SocketInterface(std::string serverAddress, int serverPort)
        : serverAddress(serverAddress), serverPort(serverPort) {
            connectToServer(SERVER_CONNECTION_TIMEOUT);
        }

    SocketInterface::~SocketInterface() {
        closeConnection();
    }

    int SocketInterface::connectToServer(int timeout) {
        LOG(INFO) << "Connecting to daemon@" << serverAddress << ":" << serverPort << "..." << std::endl;
        // Set the target time.
        std::chrono::time_point startTime = std::chrono::system_clock::now();
        std::chrono::time_point targetTime = startTime + std::chrono::seconds(timeout);
        // For each channel, try to configure and connect sockets.
        for (Channel channel : Channels) {
            LOG(INFO) << "Configuring & connecting " << getChannelName(channel) << "..." << std::endl;
            while (std::chrono::system_clock::now() < targetTime) {
                // Try configuring and connecting to the daemon server.
                int conn = socket(AF_INET, SOCK_STREAM, 0);
                if (conn < 0) {
                    LOG(ERROR) << "\tSocket creation failed." << std::endl;
                    return -1;
                }
                // Configure socket with options for low latency.
                int opt = 1;
                if (setsockopt(conn, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                    LOG(ERROR) << "\tCould not set socket option SOL_SOCKET to SO_REUSEADDR." << std::endl;
                    close(conn);
                    return -1;
                }
                if (setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
                    LOG(ERROR) << "\tCould not set socket option IPPROTO_TCP to TCP_NODELAY." << std::endl;
                    close(conn);
                    return -1;
                }
                if (setsockopt(conn, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
                    LOG(ERROR) << "\tCould not set socket option SOL_SOCKET to SO_KEEPALIVE." << std::endl;
                    close(conn);
                    return -1;
                }
                // Attempt to connect.
                sockaddr_in serverAddrDetails{};
                serverAddrDetails.sin_family = AF_INET;
                serverAddrDetails.sin_addr.s_addr = inet_addr(serverAddress.c_str());
                serverAddrDetails.sin_port = htons(serverPort);
                if (connect(conn, (struct sockaddr*)&serverAddrDetails, sizeof(serverAddrDetails)) == -1) {
                    LOG(ERROR) << "\tRetrying connection..." << std::endl;
                    close(conn);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } else {
                    LOG(INFO) << "\tConnected!" << std::endl;
                    conns.insert({channel, conn});
                    break;
                }
            }
            // If loop broken due to timeout, that's a problem.
            if (std::chrono::system_clock::now() > targetTime) {
                LOG(ERROR) << "Connection to server timed out after " << timeout << " seconds." << std::endl;
                return -1;
            }
        }
        // Set all connections to non-blocking after setup, to ensure that they've connected.
        for (Channel channel : Channels) {
            DLOG(INFO) << "Setting " << getChannelName(channel) << " to non-blocking..." << std::endl;
            int flags = fcntl(conns.at(channel), F_GETFL, 0);
            if (flags == -1) {
                LOG(ERROR) << "\tFailed to get flags for socket." << std::endl;
                return -1;
            }
            if (fcntl(conns.at(channel), F_SETFL, flags | O_NONBLOCK) == -1) {
                LOG(ERROR) << "\tFailed to set non-blocking mode for socket." << std::endl;
                return -1;
            }
        }
        LOG(INFO) << "All channels connected!" << std::endl;
        return 0;
    }

    void SocketInterface::closeConnection() {
        for (Channel channel : Channels) {
            shutdown(conns.at(channel), SHUT_WR);
            close(conns.at(channel));
        }
    }

    int SocketInterface::sendMessage(Comms::Channel channel, const std::string& message) {
        int totalBytesSent = 0;
        int totalSize = message.size();
        while (totalBytesSent < totalSize) {
            int bytesSent = send(conns.at(channel), message.data() + totalBytesSent, totalSize - totalBytesSent, 0);
            if (bytesSent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // May not be read to send yet.
                    continue;
                } else {
                    LOG(ERROR) << "Failed to send message on " << getChannelName(channel) << ": " << strerror(errno) << std::endl;
                    return -1;
                }
            }
            totalBytesSent += bytesSent;
        }
        return 0;
    }

    std::string SocketInterface::receiveMessage(Comms::Channel channel, int* timeout) {
        int* fdPtr = &conns.at(channel);
        // Set up FD for select.
        fd_set readFDs;
        FD_ZERO(&readFDs);
        FD_SET(*fdPtr, &readFDs);
        // Set up data stores.
        std::string message;
        char buffer[RECEIVE_BUFFER_SIZE];
        bool messageExists = false;
        // Wait for messages.
        timeval* t;
        if (timeout == nullptr) {
            t = nullptr;
        } else {
            timeval timeoutVal{};
            timeoutVal.tv_sec = *timeout;
            timeoutVal.tv_usec = 0;
            t = &timeoutVal;
        }
        while (true) {
            int activity = select(*fdPtr + 1, &readFDs, nullptr, nullptr, t);
            if (activity < 0) {
                LOG(ERROR) << "Select error: " << strerror(errno) << std::endl;
                return std::string();
            } else if (activity == 0) {
                LOG(WARNING) << "Timeout waiting for message on " << getChannelName(channel) << "." << std::endl;
                return std::string();
            } else {
                // Read buffer until there's nothing left.
                int bytesRead = recv(*fdPtr, buffer, RECEIVE_BUFFER_SIZE-1, 0);
                while (bytesRead > 0) {
                    messageExists = true;
                    message.append(buffer, bytesRead);
                    bytesRead = recv(*fdPtr, buffer, RECEIVE_BUFFER_SIZE-1, 0);
                }
                if (messageExists) {
                    return message;
                }
            }
        }
        return std::string();
    }

    std::future<std::string> SocketInterface::listenForMessage(Comms::Channel channel, int* timeout) {
        return std::async(std::launch::async, [this, channel, timeout]() {
            return receiveMessage(channel, timeout);
        });
    }

    NSBClient::NSBClient(const std::string& identifier, std::string serverAddress, int serverPort) : 
        comms(SocketInterface(serverAddress, serverPort)),
        originIndicator(nullptr), db(nullptr), clientId(std::move(identifier)) {}
    
    NSBClient::~NSBClient() {
        if (db) {
            delete db;
        }
        comms.closeConnection();
    }

    std::string NSBClient::msgGetPayloadObj(nsb::nsbm msg) {
        return cfg.USE_DB ? msg.msg_key() : msg.payload();
    }

    void NSBClient::msgSetPayloadObj(std::string& payloadObj, nsb::nsbm msg) {
        if (cfg.USE_DB) {
            msg.set_msg_key(std::move(payloadObj));
        } else {
            msg.set_payload(std::move(payloadObj));
        }
    }

    void NSBClient::initialize() {
        // Check to see if client is from a derived class (it should be).
        if (originIndicator == nullptr) {
            LOG(ERROR) << "NSBClient::initialize() called without setting originIndicator." << std::endl;
            return;
        }
        LOG(INFO) << "Initializing " << clientId << " with NSB daemon...";
        // Create and populate an INIT message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::INIT);
        mutableManifest->set_og(*originIndicator);
        mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
        nsb::nsbm::IntroDetails* mutableIntro = nsbMsg.mutable_intro();
        mutableIntro->set_identifier(clientId);
        // Set address and CTRL channel port.
        struct sockaddr_storage addr;
        socklen_t addrLen = sizeof(addr);
        struct sockaddr_in* s;
        if (getsockname(comms.conns.at(Comms::Channel::CTRL), (struct sockaddr*) &addr, &addrLen) == -1) {
            LOG(ERROR) << "\tgetsockname() failed to get information for the CTRL channel." << std::endl;
            return;
        }
        if (addr.ss_family == AF_INET) {
            s = (struct sockaddr_in*) &addr;
            mutableIntro->set_ch_ctrl(ntohs(s->sin_port));
            mutableIntro->set_address(inet_ntoa(s->sin_addr));
        } else {
            LOG(ERROR) << "\tOnly IPv4 (AF_INET) is currently supported." << std::endl;
            return;
        }
        // Set SEND channel port.
        if (getsockname(comms.conns.at(Comms::Channel::SEND), (struct sockaddr*) &addr, &addrLen) == -1) {
            LOG(ERROR) << "\tgetsockname() failed to get information for the SEND channel." << std::endl;
            return;
        }
        if (addr.ss_family == AF_INET) {
            s = (struct sockaddr_in*) &addr;
            mutableIntro->set_ch_send(ntohs(s->sin_port));
        } else {
            LOG(ERROR) << "\tOnly IPv4 (AF_INET) is currently supported." << std::endl;
            return;
        }
        // Set RECV channel port.
        if (getsockname(comms.conns.at(Comms::Channel::RECV), (struct sockaddr*) &addr, &addrLen) == -1) {
            LOG(ERROR) << "\tgetsockname() failed to get information for the SEND channel." << std::endl;
            return;
        }
        if (addr.ss_family == AF_INET) {
            s = (struct sockaddr_in*) &addr;
            mutableIntro->set_ch_recv(ntohs(s->sin_port));
        } else {
            LOG(ERROR) << "\tOnly IPv4 (AF_INET) is currently supported." << std::endl;
            return;
        }
        // Send the message.
        DLOG(INFO) << "Sending INIT message: " << nsbMsg.DebugString() << std::endl;
        std::string serializedMessage;
        comms.sendMessage(nsb::Comms::Channel::CTRL, nsbMsg.SerializeAsString());
        // Wait for response.
        std::string response = comms.receiveMessage(nsb::Comms::Channel::CTRL, &DAEMON_RESPONSE_TIMEOUT);
        // Check for empty string.
        if (response.empty()) {
            LOG(ERROR) << "No response received from daemon." << std::endl;
            return;
        }
        // Parse in message.
        nsb::nsbm nsbResponse = nsb::nsbm();
        nsbResponse.ParseFromString(response);
        // Check for expected operation.
        if (nsbResponse.manifest().op() == nsb::nsbm::Manifest::INIT) {
            // Get the configuration.
            if (nsbResponse.has_config()) {
                cfg = Config(nsbResponse);
                LOG(INFO) << "\tConfiguration received: Mode " << cfg.SYSTEM_MODE <<
                    " | Use DB? " << cfg.USE_DB << std::endl;
                // Set up database if necessary.
                if (cfg.USE_DB) {
                    db = new RedisConnector(clientId, cfg.DB_ADDRESS, cfg.DB_PORT);
                }
                return;
            } else {
                LOG(ERROR) << "\tNo configuration found." << std::endl;
                return;
            }
        } else {
            LOG(ERROR) << "\tUnexpected operation received: " << 
                nsb::nsbm::Manifest::Operation_Name(nsbResponse.manifest().op()) << std::endl;
        }
    }
}

int testSocketInterface() {
    using namespace nsb;
    // Testing
    LOG(INFO) << "Creating socket interface..." << std::endl;
    SocketInterface sif = SocketInterface(std::string("127.0.0.1"), 65432);
    LOG(INFO) << "Sending a message..." << std::endl;
    sif.sendMessage(Comms::Channel::CTRL, "hello");
    LOG(INFO) << "Receiving a message..." << std::endl;
    int timeout = 5;
    std::future<std::string> futureResponse = sif.listenForMessage(Comms::Channel::CTRL, &timeout);
    std::string response = futureResponse.get();
    if (response.empty()) {
        LOG(ERROR) << "\tNo response received." << std::endl;
    } else {
        LOG(INFO) << "\tReceived response: " << response << std::endl;
    }
    LOG(INFO) << "Disconnecting socket interace..." << std::endl;
    sif.closeConnection();
    LOG(INFO) << "Done!" << std::endl;
    return 0;
}

int testRedisConnector() {
    using namespace nsb;
    // Testing Redis Connector
    std::string thisAppId = "app1";
    std::string thatAppId = "app2";
    std::string redisServerAddr = "127.0.0.1";
    RedisConnector thisConn = RedisConnector(thisAppId, redisServerAddr, 5050);
    RedisConnector thatConn = RedisConnector(thatAppId, redisServerAddr, 5050);
    std::string sendPayload = "hola mundo";
    std::string key = thisConn.store(sendPayload);
    std::string recvPayload = thatConn.checkOut(key);
    DLOG(INFO) << "Payload sent: " << sendPayload << std::endl;
    DLOG(INFO) << "Payload received: " << recvPayload << std::endl;
    return 0;
}

int testLifecycle() {
    using namespace nsb;
    // Create app client.
    const std::string idApp1 = "app1";
    std::string nsbDaemonAddr = "127.0.0.1";
    int nsbDaemonPort = 65432;
    NSBAppClient app1 = NSBAppClient(idApp1, nsbDaemonAddr, nsbDaemonPort);
    
}

int main() {
    using namespace nsb;
    // Set up logging.
    NsbLogSink log_output = NsbLogSink();
    absl::InitializeLog();
    absl::log_internal::AddLogSink(&log_output);
    // return testSocketInterface();
    return testRedisConnector();
}