// nsb.cc

#include "nsb_client.h"

namespace nsb {

    NSBClient::NSBClient(const std::string& identifier, std::string serverAddress, int serverPort) : 
        comms(SocketInterface(serverAddress, serverPort)),
        originIndicator(nullptr), db(nullptr), clientId(std::move(identifier)) {}
    
    NSBClient::~NSBClient() {
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
                LOG(INFO) << "INIT: Configuration received: Mode " << (int) cfg.SYSTEM_MODE
                          << " | Sim " << (int) cfg.SIMULATOR_MODE
                          << " | Use DB? " << cfg.USE_DB << std::endl;
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

    std::string NSBAppClient::send(const std::string destId, std::string payload) {
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
        // Set return to "" by default.
        std::string key = "";
        if (cfg.USE_DB) {
            // Store the payload in the database and get the key.
            key = db->store(payload);
            nsbMsg.set_msg_key(key);
        } else {
            nsbMsg.set_payload(payload);
        }
        // Send the message.
        DLOG(INFO) << "SEND: Sending message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::SEND, nsbMsg.SerializeAsString());
        // Return key in case it's useful.
        return key;
    }

    MessageEntry NSBAppClient::receive(std::string* destId, int timeout) {
        nsb::nsbm* nsbMsg = new nsb::nsbm();
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            // Create and populate a RECEIVE message.
            nsb::nsbm::Manifest* mutableManifest = nsbMsg->mutable_manifest();
            mutableManifest->set_op(nsb::nsbm::Manifest::RECEIVE);
            mutableManifest->set_og(*originIndicator);
            mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
            // If destId is not specified, set it to its own ID.
            if (destId == nullptr) {
                destId = new std::string(clientId);
            }
            nsbMsg->mutable_metadata()->set_dest_id(*destId);
            // Send the message.
            DLOG(INFO) << "RECV: Sending request:" << std::endl << nsbMsg->DebugString();
            comms.sendMessage(nsb::Comms::Channel::RECV, nsbMsg->SerializeAsString());
        }
        // Wait for response or incoming message.
        std::string response = comms.receiveMessage(nsb::Comms::Channel::RECV, &timeout);
        if (response.empty()) {
            LOG(ERROR) << "RECV: No response received from daemon." << std::endl;
            return MessageEntry();
        }
        // Parse in message.
        nsbMsg->ParseFromString(response);
        nsb::nsbm::Manifest manifest = nsbMsg->manifest();
        if (manifest.op() != nsb::nsbm::Manifest::RECEIVE && manifest.op() != nsb::nsbm::Manifest::FORWARD) {
            LOG(ERROR) << "RECV: Unexpected operation over RECV channel." << std::endl;
            return MessageEntry();
        } else if (manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
            // Repack it in MessageEntry format with the full payload.
            std::string payload = cfg.USE_DB ? db->checkOut(nsbMsg->msg_key())
                                             : nsbMsg->payload();
            MessageEntry receivedPayload = MessageEntry(
                nsbMsg->metadata().src_id(),
                nsbMsg->metadata().dest_id(),
                payload,
                nsbMsg->metadata().payload_size()
            );
            return receivedPayload;
        } else if (manifest.code() == nsb::nsbm::Manifest::NO_MESSAGE) {
            if (destId != nullptr) {
                LOG(INFO) << "RECV: No message found for destination " << *destId << "." << std::endl;
            } else {
                LOG(INFO) << "RECV: No messages found for any destination." << std::endl;
            }
            LOG(INFO) << "RECV: No message found for destination " << *destId << "." << std::endl;
            return MessageEntry();
        } else {
            LOG(ERROR) << "RECV: Unexpected status code returned from receive." << std::endl;
            return MessageEntry();
        }
    }

    NSBSimClient::NSBSimClient(const std::string& identifier, std::string& serverAddress, int serverPort) : 
        NSBClient(identifier, serverAddress, serverPort) {
        originIndicator = new nsb::nsbm::Manifest::Originator(nsb::nsbm::Manifest::SIM_CLIENT);
        initialize();
    }

    NSBSimClient::~NSBSimClient() {}

    MessageEntry NSBSimClient::fetch(std::string* srcId, int timeout) {
        nsb::nsbm* nsbMsg = new nsb::nsbm();
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            // Create and populate a FETCH message.
            nsb::nsbm::Manifest* mutableManifest = nsbMsg->mutable_manifest();
            mutableManifest->set_op(nsb::nsbm::Manifest::FETCH);
            mutableManifest->set_og(*originIndicator);
            mutableManifest->set_code(nsb::nsbm::Manifest::SUCCESS);
            if (cfg.SIMULATOR_MODE == Config::SimulatorMode::SYSTEM_WIDE) {
                if (srcId != nullptr) {
                    // If target source ID has been set, specify that. 
                    nsbMsg->mutable_metadata()->set_src_id(*srcId);
                } // Otherwise, we can leave it unspecified.
            } else if (cfg.SIMULATOR_MODE == Config::SimulatorMode::PER_NODE) {
                if (srcId != nullptr) {
                    LOG(WARNING)
                        << "Simulation mode is set to PER_NODE, so specified target source will be overwritten."
                        << std::endl;
                }
                nsbMsg->mutable_metadata()->set_src_id(clientId);
            }
            // Send the message.
            DLOG(INFO) << "FETCH: Sending request:" << std::endl << nsbMsg->DebugString();
            comms.sendMessage(nsb::Comms::Channel::RECV, nsbMsg->SerializeAsString());
        }
        // Wait for response or incoming message.
        std::string response = comms.receiveMessage(nsb::Comms::Channel::RECV, &timeout);
        if (response.empty()) {
            LOG(ERROR) << "FETCH: No response received from daemon." << std::endl;
            return MessageEntry();
        }
        // Parse in message.
        nsbMsg->ParseFromString(response);
        DLOG(INFO) << "FETCH: Response:" << std::endl << nsbMsg->DebugString();
        nsb::nsbm::Manifest manifest = nsbMsg->manifest();
        if (manifest.op() != nsb::nsbm::Manifest::FETCH && manifest.op() != nsb::nsbm::Manifest::FORWARD) {
            LOG(ERROR) << "FETCH: Unexpected operation over RECV channel." << std::endl;
            delete nsbMsg;
            return MessageEntry();
        } else if (manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
            // Repack it in MessageEntry format with the full payload.
            std::string payload = cfg.USE_DB ? db->checkOut(nsbMsg->msg_key())
                                             : nsbMsg->payload();
            MessageEntry fetchedMessage = MessageEntry(
                nsbMsg->metadata().src_id(),
                nsbMsg->metadata().dest_id(),
                payload,
                nsbMsg->metadata().payload_size()
            );
            return fetchedMessage;
        } else if (manifest.code() == nsb::nsbm::Manifest::NO_MESSAGE) {
            if (srcId != nullptr) {
                LOG(INFO) << "FETCH: No message found for source " << *srcId << "." << std::endl;
            } else {
                LOG(INFO) << "FETCH: No messages found for any source." << std::endl;
            }
            
            return MessageEntry();
        } else {
            LOG(ERROR) << "FETCH: Unexpected status code returned from receive." << std::endl;
            return MessageEntry();
        }
    }

    std::string NSBSimClient::post(std::string srcId, std::string destId, std::string &payload) {
        // // Create and populate a POST message.
        // nsb::nsbm nsbMsg = nsb::nsbm();
        // nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        // mutableManifest->set_op(nsb::nsbm::Manifest::POST);
        // mutableManifest->set_og(*originIndicator);
        // if (success) {
        //     mutableManifest->set_code(nsb::nsbm::Manifest::MESSAGE);
        // } else {
        //     mutableManifest->set_code(nsb::nsbm::Manifest::NO_MESSAGE);
        // }
        // nsb::nsbm::Metadata* mutableMetadata = nsbMsg.mutable_metadata();
        // mutableMetadata->set_src_id(srcId);
        // mutableMetadata->set_dest_id(destId);
        // mutableMetadata->set_payload_size(payloadSize);
        // if (cfg.USE_DB) {
        //     nsbMsg.set_msg_key(payloadObj);
        // } else {
        //     nsbMsg.set_payload(payloadObj);
        // }
        // // Send the message.
        // DLOG(INFO) << "POST: Posting message:" << std::endl << nsbMsg.DebugString();
        // comms.sendMessage(nsb::Comms::Channel::SEND, nsbMsg.SerializeAsString());

        // Create and populate a SEND message.
        nsb::nsbm nsbMsg = nsb::nsbm();
        nsb::nsbm::Manifest* mutableManifest = nsbMsg.mutable_manifest();
        mutableManifest->set_op(nsb::nsbm::Manifest::POST);
        mutableManifest->set_og(*originIndicator);
        mutableManifest->set_code(nsb::nsbm::Manifest::MESSAGE);
        nsb::nsbm::Metadata* mutableMetadata = nsbMsg.mutable_metadata();
        mutableMetadata->set_src_id(clientId);
        mutableMetadata->set_dest_id(destId);
        mutableMetadata->set_payload_size(static_cast<int>(payload.size()));
        // Set return to "" by default.
        std::string key = "";
        if (cfg.USE_DB) {
            // Store the payload in the database and get the key.
            key = db->store(payload);
            nsbMsg.set_msg_key(key);
        } else {
            nsbMsg.set_payload(payload);
        }
        // Post the message.
        DLOG(INFO) << "POST: Posting message:" << std::endl << nsbMsg.DebugString();
        comms.sendMessage(nsb::Comms::Channel::SEND, nsbMsg.SerializeAsString());
        // Return key in case it's useful.
        return key;
    }
}