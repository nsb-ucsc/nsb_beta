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

    void NSBAppClient::send(const std::string& destId, std::string& payload, std::string* key = nullptr) {
        // Create and populate a SEND message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::SEND);
        mutableManifest->set_og(*originIndicator);
        mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
        if (cfg.USE_DB) {
            if (key == nullptr) {
                LOG(ERROR) << "SEND: Key must be provided when using a database." << std::endl;
                return;
            }
            // Store the payload in the database and get the key.
            *key = db->store(payload);
            nsbMsg.set_msg_key(*key);
        } else {
            nsbMsg.set_payload(payload);
        }
        // Send the message.
        DLOG(INFO) << "SEND: Sending message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::SEND, nsbMsg.SerializeAsString());
    }

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
    nsb::nsbm* NSBAppClient::receive(int* destId, int timeout) {
        nsb::nsbm* nsbMsg = new nsb::nsbm();
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            // Create and populate a RECEIVE message.
            nsb::nsbm::Manifest* mutableManifest = nsbMsg->mutable_manifest();
            mutableManifest->set_op(nsb::nsbm::Manifest::RECEIVE);
            mutableManifest->set_og(*originIndicator);
            mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
            nsbMsg->mutable_metadata()->set_dest_id(*destId);
            // Send the message.
            DLOG(INFO) << "RECV: Sending request:" << std::endl << nsbMsg->DebugString();
            comms.sendMessage(nsb::Comms::Channel::RECV, nsbMsg->SerializeAsString());
            // Wait for response.
            std::string response = comms.receiveMessage(nsb::Comms::Channel::RECV, &timeout);
            if (response.empty()) {
                LOG(ERROR) << "RECV: No response received from daemon." << std::endl;
                return nullptr;
            }
        } else if (cfg.SYSTEM_MODE == Config::SystemMode::PUSH) {
            // Listen for a message on the RECV channel.
            std::string response = comms.listenForMessage(nsb::Comms::Channel::RECV, &timeout).get();
            nsb::nsbm* nsbMsg = new nsb::nsbm();
            nsbMsg->ParseFromString(response);
        }
        // Parse in message.
        nsb::nsbm::Manifest manifest = nsbMsg->manifest();
        if (manifest.op() != nsb::nsbm::Manifest::RECEIVE) {
            LOG(ERROR) << "RECV: Unexpected operation over RECV channel." << std::endl;
            delete nsbMsg;
            return nullptr;
        } else if (manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
            if (cfg.USE_DB) {
                // Get the actual payload.
                const std::string payload = db->checkOut(nsbMsg->msg_key());
                nsbMsg->set_payload(payload);
            }
            return nsbMsg;
        } else if (manifest.code() == nsb::nsbm::Manifest::NO_MESSAGE) {
            LOG(INFO) << "RECV: No message found for destination " << *destId << "." << std::endl;
            delete nsbMsg;
            return nullptr;
        } else {
            LOG(ERROR) << "RECV: Unexpected status code returned from receive." << std::endl;
            delete nsbMsg;
            return nullptr;
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
    const std::string idApp2 = "app2";
    std::string nsbDaemonAddr = "127.0.0.1";
    int nsbDaemonPort = 65432;
    NSBAppClient app1 = NSBAppClient(idApp1, nsbDaemonAddr, nsbDaemonPort);
    NSBAppClient app2 = NSBAppClient(idApp2, nsbDaemonAddr, nsbDaemonPort);
    app1.ping();
    app2.ping();
    // Send a message.
    std::string payload = "Hello from app1";
    std::string key = "";
    app1.send(idApp2, payload, &key);
    // Receive a message.
    nsb::nsbm* received = app2.receive(nullptr, DAEMON_RESPONSE_TIMEOUT);
    if (received == nullptr) {
        LOG(ERROR) << "Failed to receive message." << std::endl;
        app1.exit();
        return -1;
    } else {

    }
    // Exit.
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