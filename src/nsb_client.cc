// nsb.cc

#include "nsb_client.h"

namespace nsb {

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
            LOG(ERROR) << "INIT: initialize() called without setting originIndicator." << std::endl;
            return;
        }
        LOG(INFO) << "INIT: Initializing " << clientId << " with NSB daemon..." << std::endl;
        // Create and populate an INIT message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::INIT);
        mutableManifest->set_og(*originIndicator);
        mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
        nsb::nsbm::IntroDetails* mutableIntro = nsbMsg.mutable_intro();
        mutableIntro->set_identifier(clientId);
        // Function to get and set address and channel port information.
        auto getSetChannelAddrPort = [&](Comms::Channel channel, bool setAddress) {
            struct sockaddr_storage addr;
            socklen_t addrLen = sizeof(addr);
            struct sockaddr_in* s;
            if (getsockname(comms.conns.at(channel), (struct sockaddr*) &addr, &addrLen) == -1) {
                LOG(ERROR) << "INIT: getsockname() failed to get information for the "
                    << comms.getChannelName(channel) << " channel." << std::endl;
                return 1;
            }
            if (addr.ss_family == AF_INET) {
                s = (struct sockaddr_in*) &addr;
                mutableIntro->set_ch_ctrl(ntohs(s->sin_port));
                if (setAddress) {
                    mutableIntro->set_address(inet_ntoa(s->sin_addr));
                }
                return 0;
            } else {
                LOG(ERROR) << "INIT: Only IPv4 (AF_INET) is currently supported." << std::endl;
                return 1;
            }
        };
        // Set channel information.
        getSetChannelAddrPort(Comms::Channel::CTRL, true);
        getSetChannelAddrPort(Comms::Channel::SEND, false);
        getSetChannelAddrPort(Comms::Channel::RECV, false);
        // Send the message.
        DLOG(INFO) << "INIT: Sending message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::CTRL, nsbMsg.SerializeAsString());
        // Wait for response.
        int timeout = DAEMON_RESPONSE_TIMEOUT;
        std::string response = comms.receiveMessage(nsb::Comms::Channel::CTRL, &timeout);
        // Check for empty string.
        if (response.empty()) {
            LOG(ERROR) << "INIT: No response received from daemon." << std::endl;
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
                LOG(INFO) << "INIT: Configuration received: Mode " << (int) cfg.SYSTEM_MODE <<
                    " | Use DB? " << cfg.USE_DB << std::endl;
                // Set up database if necessary.
                if (cfg.USE_DB) {
                    db = new RedisConnector(clientId, cfg.DB_ADDRESS, cfg.DB_PORT);
                    LOG(INFO) << "INIT: Connected to RedisConnecter@" << cfg.DB_ADDRESS << ":" << cfg.DB_PORT;
                }
                return;
            } else {
                LOG(ERROR) << "INIT: No configuration found." << std::endl;
                return;
            }
        } else {
            LOG(ERROR) << "INIT: Unexpected operation received: " << 
                nsb::nsbm::Manifest::Operation_Name(nsbResponse.manifest().op()) << std::endl;
        }
    }

    bool NSBClient::ping() {
        // Check to see if client is from a derived class (it should be).
        if (originIndicator == nullptr) {
            LOG(ERROR) << "PING: ping() called without setting originIndicator." << std::endl;
            return false;
        }
        LOG(INFO) << "PING: Pinging NSB Daemon from " << clientId << "..." << std::endl;
        // Create and populate a PING message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::PING);
        mutableManifest->set_og(*originIndicator);
        mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
        // Send the message.
        DLOG(INFO) << "PING: Sending message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::CTRL, nsbMsg.SerializeAsString());
        // Wait for response.
        int timeout = DAEMON_RESPONSE_TIMEOUT;
        std::string response = comms.receiveMessage(nsb::Comms::Channel::CTRL, &timeout);
        // Check for empty string.
        if (response.empty()) {
            LOG(ERROR) << "PING: No response received from daemon." << std::endl;
            return false;
        }
        // Parse in message.
        nsb::nsbm nsbResponse = nsb::nsbm();
        nsbResponse.ParseFromString(response);
        // Check for expected operation.
        if (nsbResponse.manifest().op() == nsb::nsbm::Manifest::PING) {
            // Get the configuration.
            if (nsbResponse.manifest().code() == nsb::nsbm::Manifest::SUCCESS) {
                LOG(INFO) << "PING: Server has pinged back!" << std::endl;
                return true;
            } else if (nsbResponse.manifest().code() == nsb::nsbm::Manifest::FAILURE) {
                LOG(ERROR) << "PING: Server ping failed." << std::endl;
                return false;
            } else {
                LOG(ERROR) << "PING: Unexpected status code returned from ping." << std::endl;
                return false;
            }
        } else {
            LOG(ERROR) << "PING: Unexpected operation received: " << 
                nsb::nsbm::Manifest::Operation_Name(nsbResponse.manifest().op()) << std::endl;
        }
        return false;
    }

    void NSBClient::exit() {
        // Create and populate a PING message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::EXIT);
        mutableManifest->set_og(*originIndicator);
        mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
        // Send the message.
        DLOG(INFO) << "EXIT: Sending message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::CTRL, nsbMsg.SerializeAsString());
    }
    
    NSBAppClient::NSBAppClient(const std::string& identifier, std::string& serverAddress, int serverPort) : 
        NSBClient(identifier, serverAddress, serverPort) {
            originIndicator = new nsb::nsbm::Manifest::Originator(nsb::nsbm::Manifest::APP_CLIENT);
            initialize();
        }

    NSBAppClient::~NSBAppClient() {
        if (originIndicator) {
            delete originIndicator;
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
    app1.ping();
    app1.exit();
    return 0;
}

int main() {
    using namespace nsb;
    // Set up logging.
    NsbLogSink log_output = NsbLogSink();
    absl::InitializeLog();
    absl::log_internal::AddLogSink(&log_output);
    // return testSocketInterface();
    // return testRedisConnector();
    return testLifecycle();
}