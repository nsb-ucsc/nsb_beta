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

    void NSBClient::msgSetPayloadObj(std::string payloadObj, nsb::nsbm msg) {
        if (cfg.USE_DB) {
            msg.set_msg_key(payloadObj);
        } else {
            msg.set_payload(payloadObj);
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
                return -1;
            }
            if (addr.ss_family == AF_INET) {
                s = (struct sockaddr_in*) &addr;
                switch(channel) {
                    case Comms::Channel::CTRL: mutableIntro->set_ch_ctrl(ntohs(s->sin_port)); break;
                    case Comms::Channel::SEND: mutableIntro->set_ch_send(ntohs(s->sin_port)); break;
                    case Comms::Channel::RECV: mutableIntro->set_ch_recv(ntohs(s->sin_port)); break;
                    default: LOG(ERROR) << "INIT: Unexpected channel. Exiting initialization." << std::endl; return -1;
                }
                if (setAddress) {
                    mutableIntro->set_address(inet_ntoa(s->sin_addr));
                }
                return 0;
            } else {
                LOG(ERROR) << "INIT: Only IPv4 (AF_INET) is currently supported." << std::endl;
                return -1;
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
            if (nsbResponse.manifest().code() != nsb::nsbm::Manifest::SUCCESS) {
                LOG(ERROR) << "INIT: Initialization failed." << std::endl;
                std::exit(EXIT_FAILURE);
            }
            // Get the configuration.
            if (nsbResponse.has_config()) {
                cfg = Config(nsbResponse);
                LOG(INFO) << "INIT: Configuration received: Mode " << (int) cfg.SYSTEM_MODE <<
                    " | Use DB? " << cfg.USE_DB << std::endl;
                // Set up database if necessary.
                if (cfg.USE_DB) {
                    db = new RedisConnector(clientId, cfg.DB_ADDRESS, cfg.DB_PORT);
                    if (db->isConnected()) {
                        LOG(INFO) << "INIT: Connected to RedisConnecter@" << cfg.DB_ADDRESS << ":" << cfg.DB_PORT;
                    } else {
                        LOG(ERROR) << "INIT: Failed to connect to Redis server. Ensure that it is online." << std::endl;
                        std::exit(EXIT_FAILURE);
                    }
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
        mutableManifest->set_code(nsb::nsbm::Manifest::MESSAGE);
        nsb::nsbm::Metadata* mutableMetadata = nsbMsg.mutable_metadata();
        mutableMetadata->set_src_id(clientId);
        mutableMetadata->set_dest_id(destId);
        mutableMetadata->set_payload_size(static_cast<int>(payload.size()));
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

    nsb::nsbm* NSBAppClient::receive(std::string* destId, int timeout) {
        nsb::nsbm* nsbMsg = new nsb::nsbm();
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            // Create and populate a RECEIVE message.
            nsb::nsbm::Manifest* mutableManifest = nsbMsg->mutable_manifest();
            mutableManifest->set_op(nsb::nsbm::Manifest::RECEIVE);
            mutableManifest->set_og(*originIndicator);
            mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
            if (destId != nullptr) {
                nsbMsg->mutable_metadata()->set_dest_id(*destId);
            }
            // Send the message.
            DLOG(INFO) << "RECV: Sending request:" << std::endl << nsbMsg->DebugString();
            comms.sendMessage(nsb::Comms::Channel::RECV, nsbMsg->SerializeAsString());
        }
        // Wait for response or incoming message.
        std::string response = comms.receiveMessage(nsb::Comms::Channel::RECV, &timeout);
        if (response.empty()) {
            LOG(ERROR) << "RECV: No response received from daemon." << std::endl;
            return nullptr;
        }
        // Parse in message.
        nsbMsg->ParseFromString(response);
        nsb::nsbm::Manifest manifest = nsbMsg->manifest();
        if (manifest.op() != nsb::nsbm::Manifest::RECEIVE && manifest.op() != nsb::nsbm::Manifest::FORWARD) {
            LOG(ERROR) << "RECV: Unexpected operation over RECV channel." << std::endl;
            delete nsbMsg;
            return nullptr;
        } else if (manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
            if (cfg.USE_DB) {
                // Get the actual payload.
                std::string payload = db->checkOut(nsbMsg->msg_key());
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

    NSBSimClient::NSBSimClient(const std::string& identifier, std::string& serverAddress, int serverPort) : 
        NSBClient(identifier, serverAddress, serverPort) {
        originIndicator = new nsb::nsbm::Manifest::Originator(nsb::nsbm::Manifest::SIM_CLIENT);
        initialize();
    }

    NSBSimClient::~NSBSimClient() {
        if (originIndicator) {
            delete originIndicator;
        }
    }

    nsb::nsbm* NSBSimClient::fetch(std::string* srcId, int timeout, bool getPayload, std::string* payload=nullptr) {
        nsb::nsbm* nsbMsg = new nsb::nsbm();
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            // Create and populate a FETCH message.
            nsb::nsbm::Manifest* mutableManifest = nsbMsg->mutable_manifest();
            mutableManifest->set_op(nsb::nsbm::Manifest::FETCH);
            mutableManifest->set_og(*originIndicator);
            mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
            if (srcId != nullptr) {
                nsbMsg->mutable_metadata()->set_src_id(*srcId);
            }
            // Send the message.
            DLOG(INFO) << "FETCH: Sending request:" << std::endl << nsbMsg->DebugString();
            comms.sendMessage(nsb::Comms::Channel::RECV, nsbMsg->SerializeAsString());
        }
        // Wait for response or incoming message.
        std::string response = comms.receiveMessage(nsb::Comms::Channel::RECV, &timeout);
        if (response.empty()) {
            LOG(ERROR) << "FETCH: No response received from daemon." << std::endl;
            return nullptr;
        }
        // Parse in message.
        nsbMsg->ParseFromString(response);
        DLOG(INFO) << "FETCH: Response:" << std::endl << nsbMsg->DebugString();
        nsb::nsbm::Manifest manifest = nsbMsg->manifest();
        if (manifest.op() != nsb::nsbm::Manifest::FETCH && manifest.op() != nsb::nsbm::Manifest::FORWARD) {
            LOG(ERROR) << "FETCH: Unexpected operation over RECV channel." << std::endl;
            delete nsbMsg;
            return nullptr;
        } else if (manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
            if (cfg.USE_DB && getPayload) {
                // Get the actual payload.
                const std::string peekedPayload = db->peek(nsbMsg->msg_key());
                payload->assign(peekedPayload);
            }
            return nsbMsg;
        } else if (manifest.code() == nsb::nsbm::Manifest::NO_MESSAGE) {
            LOG(INFO) << "FETCH: No message found for source " << *srcId << "." << std::endl;
            delete nsbMsg;
            return nullptr;
        } else {
            LOG(ERROR) << "FETCH: Unexpected status code returned from receive." << std::endl;
            delete nsbMsg;
            return nullptr;
        }
    }

    void NSBSimClient::post(std::string srcId, std::string destId, std::string payloadObj,
        int payloadSize, bool success) {
        // Create and populate a POST message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::POST);
        mutableManifest->set_og(*originIndicator);
        if (success) {
            mutableManifest->set_code(nsb::nsbm::Manifest::MESSAGE);
        } else {
            mutableManifest->set_code(nsb::nsbm::Manifest::NO_MESSAGE);
        }
        nsb::nsbm::Metadata* mutableMetadata = nsbMsg.mutable_metadata();
        mutableMetadata->set_src_id(srcId);
        mutableMetadata->set_dest_id(destId);
        mutableMetadata->set_payload_size(payloadSize);
        if (cfg.USE_DB) {
            nsbMsg.set_msg_key(payloadObj);
        } else {
            nsbMsg.set_payload(payloadObj);
        }
        // Send the message.
        DLOG(INFO) << "POST: Posting message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::SEND, nsbMsg.SerializeAsString());
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
    const std::string idApp1 = "node1";
    const std::string idApp2 = "node2";
    const std::string idSim1 = "node1";
    const std::string idSim2 = "node2";
    std::string nsbDaemonAddr = "127.0.0.1";
    int nsbDaemonPort = 65432;
    NSBAppClient app1 = NSBAppClient(idApp1, nsbDaemonAddr, nsbDaemonPort);
    NSBAppClient app2 = NSBAppClient(idApp2, nsbDaemonAddr, nsbDaemonPort);
    NSBSimClient sim1 = NSBSimClient(idSim1, nsbDaemonAddr, nsbDaemonPort);
    NSBSimClient sim2 = NSBSimClient(idSim1, nsbDaemonAddr, nsbDaemonPort);
    app1.ping();
    app2.ping();
    sim1.ping();
    sim2.ping();
    // Send a message.
    std::string payload = "Hello from app1";
    std::string *key = new std::string();
    app1.send(idApp2, payload, key);
    // Go through the simulator.
    nsb::nsbm* fetchedMsg = sim1.fetch(nullptr, 5, false);
    if (fetchedMsg == nullptr) {
        LOG(ERROR) << "Failed to fetch message." << std::endl;
        app1.exit();
        return -1;
    }
    sim1.post(fetchedMsg->metadata().src_id(), fetchedMsg->metadata().dest_id(),
              fetchedMsg->msg_key(), fetchedMsg->metadata().payload_size(), true);
    // Receive a message.
    nsb::nsbm* receivedMsg = app2.receive(nullptr, 5);
    if (receivedMsg == nullptr) {
        LOG(ERROR) << "Failed to receive message." << std::endl;
        app1.exit();
        return -1;
    } else {
        LOG(INFO) << "Received message: " << receivedMsg->payload() << std::endl;
        delete receivedMsg;  // Clean up the received message.
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