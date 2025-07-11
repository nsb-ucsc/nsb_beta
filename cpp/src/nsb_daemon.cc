// nsb_daemon.cc

#include "nsb_daemon.h"

namespace nsb {

    NSBDaemon::NSBDaemon(int s_port, std::string filename) : running(false), server_port(s_port) {
        GOOGLE_PROTOBUF_VERIFY_VERSION;
        configure(filename);
    }

    NSBDaemon::~NSBDaemon() {
        // If the server is running, stop it.
        if (running) {
            stop();
        }
        google::protobuf::ShutdownProtobufLibrary();
    }

    void NSBDaemon::start() {
        // If the server isn't running already, start it.
        if (!running) {
            running = true;
            start_server(server_port);
            LOG(INFO) << "NSBDaemon started." << std::endl;
        }
    }

    void NSBDaemon::configure(std::string filename) {
        // Open YAML file.
        YAML::Node config = YAML::LoadFile(filename);
        // Check if the file is valid.
        if (config.IsNull()) {
            std::cerr << "Failed to load configuration file: " << filename << std::endl;
            return;
        }
        // Parse the configuration file.
        int sys_mode = config["system"]["mode"].as<int>();
        cfg.SYSTEM_MODE = static_cast<Config::SystemMode>(sys_mode);
        int sim_mode = config["system"]["simulator_mode"].as<int>();
        cfg.SIMULATOR_MODE = static_cast<Config::SimulatorMode>(sim_mode);
        cfg.USE_DB = config["database"]["use_db"].as<bool>();
        if (cfg.USE_DB) {
            cfg.DB_ADDRESS = config["database"]["db_address"].as<std::string>();
            cfg.DB_PORT = config["database"]["db_port"].as<int>();
        }
    }

    void NSBDaemon::start_server(int port) {
        // Set file descriptor and address information.
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            perror("Socket creation failed.");
            return;
        }
        // Set to accept multiple connections.
        const int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("Set socket options failed.");
            close(server_fd);
            return;
        }
        // Set non-blocking.
        int flags = fcntl(server_fd, F_GETFL, 0);
        if (flags == -1) {
            perror("Get socket flags failed.");
            close(server_fd);
            return;
        }
        if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("Set socket flags failed.");
            close(server_fd);
            return;
        }
        // Create address.
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(port);
        // Bind and listen on port.
        if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            std::string error_msg = "Bind failed on address " + std::string(inet_ntoa(server_addr.sin_addr)) + 
                                    " on port " + std::to_string(ntohs(server_addr.sin_port)) + ".";
            LOG(ERROR) << error_msg << std::endl;
            close(server_fd);
            return;
        }
        if (listen(server_fd, SOMAXCONN) == -1) {
            LOG(ERROR) << "Listen failed." << std::endl;
            close(server_fd);
            return;
        }
        LOG(INFO) << "Server started on port " << port << std::endl;

        // Run server.
        fd_set read_fds;
        // Create vector to track client file descriptors.
        std::vector<int> channel_fds;
        while (running) {
            // Set server file descriptor.
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);
            int max_fd = server_fd;
            // Set client file descriptors.
            for (int channel_fd : channel_fds) {
                FD_SET(channel_fd, &read_fds);
                max_fd = std::max(max_fd, channel_fd);
            }
            // Monitor select for activity on the file descriptors.
            timeval timeout{};
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (activity < 0) {
                // Check for errors, but excuse ones that come from non-blocking.
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Select error.");
                    break;
                }
            } else if (activity > 0) {
                // First, monitor existing connections through client FDs.
                for (auto it=channel_fds.begin(); it!=channel_fds.end();) {
                    int fd = *it;
                    // Check to see if there's action on this client FD.
                    if (FD_ISSET(fd, &read_fds)) {
                        bool message_exists = false;
                        char buffer[MAX_BUFFER_SIZE];
                        std::vector<char> message;
                        // Read buffer until there's nothing left.
                        int bytes_read = recv(fd, buffer, sizeof(buffer)-1, 0);
                        while(bytes_read > 0) {
                            message_exists = true;
                            DLOG(INFO) << "Picked up " << bytes_read << "B from FD " << fd << "." << std::endl;
                            message.insert(message.end(), buffer, buffer+bytes_read);
                            bytes_read = recv(fd, buffer, sizeof(buffer)-1, 0);
                        }
                        if (message_exists) {
                            DLOG(INFO) << "Received message from FD " << fd << ": " << 
                                std::string(message.begin()+1, message.end()) << std::endl;
                            handle_message(fd, message);
                            ++it;
                        }
                        else {
                            LOG(WARNING) << "Disconnected from FD " << fd << "." << std::endl;
                            shutdown(fd, SHUT_WR);
                            close(fd);
                            channel_fds.erase(it);
                        }
                    }
                    else {++it;}
                }
                // Then handle any new connections.
                if (FD_ISSET(server_fd, &read_fds)) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int channel_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if (channel_fd == -1) {
                        LOG(ERROR) << "Accept failed." << std::endl;
                        continue;
                    }
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
                    int client_port = ntohs(client_addr.sin_port);
                    LOG(INFO) << "Channel connected from IP: " << client_ip 
                            << ", Port: " << client_port << "." << std::endl;
                    // Add to the FD lookup.
                    std::string key = std::string(client_ip) + ":" + std::to_string(client_port);
                    fd_lookup.emplace(key, channel_fd);
                    // Add client FD to the pool of client FDs.
                    channel_fds.push_back(channel_fd);
                }
            }
        }
        LOG(INFO) << "Server is no longer running, closing connections..." << std::endl;
        // When running stops, close connections and close server.
        for (int channel_fd : channel_fds) {
            DLOG(INFO) << "Closing connection to FD " << channel_fd << "." << std::endl;
            close(channel_fd);
        }
        close(server_fd);
        LOG(INFO) << "Server stopped." << std::endl;
    }

    void NSBDaemon::handle_message(int fd, std::vector<char> message) {
        nsb::nsbm nsb_message;
        nsb_message.ParseFromArray(message.data(), message.size());
        nsb::nsbm::Manifest manifest = nsb_message.manifest();
        DLOG(INFO) << "Manifest " << nsb::nsbm::Manifest::Operation_Name(manifest.op()) << "<--" 
                   << nsb::nsbm::Manifest::Originator_Name(manifest.og())
                   << " received from FD " << fd << "." << std::endl;
        // Get message fields.
        nsb::nsbm::Metadata metadata = nsb_message.metadata();
        // Prepare template for response.
        nsb::nsbm nsb_response;
        nsb::nsbm::Manifest* r_manifest = nsb_response.mutable_manifest();
        bool response_required = false;
        // Redirect handling based on specified operation.
        switch (manifest.op()) {
            case nsb::nsbm::Manifest::INIT:
                handle_init(&nsb_message, &nsb_response, &response_required);
                break;
            case nsb::nsbm::Manifest::PING:
                handle_ping(&nsb_message, &nsb_response, &response_required);
                break;
            case nsb::nsbm::Manifest::SEND:
                handle_send(&nsb_message, &nsb_response, &response_required);
                break;
            case nsb::nsbm::Manifest::FETCH:
                handle_fetch(&nsb_message, &nsb_response, &response_required);
                break;
            case nsb::nsbm::Manifest::POST:
                handle_post(&nsb_message, &nsb_response, &response_required);
                break;
            case nsb::nsbm::Manifest::RECEIVE:
                handle_receive(&nsb_message, &nsb_response, &response_required);
                break;
            case nsb::nsbm::Manifest::EXIT:
                LOG(INFO) << "Exiting." << std::endl;
                // Stop the daemon.
                stop();
                break;
            default:
                LOG(ERROR) << "Unknown operation: " << manifest.op() << std::endl;
                // Create a negative PING response if confused.
                r_manifest->set_op(nsb::nsbm::Manifest::PING);
                r_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
                r_manifest->set_code(nsb::nsbm::Manifest::FAILURE);
                response_required = true;
        }
        // Send response if required.
        if (response_required) {
            std::size_t size = nsb_response.ByteSizeLong();
            void* r_buffer = malloc(size);
            nsb_response.SerializeToArray(r_buffer, size);
            DLOG(INFO) << "Sending response back: (" << size << "B) " << r_buffer << std::endl;
            send(fd, r_buffer, size, 0);
        }
    }

    void NSBDaemon::handle_init(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
        bool success = false;
        LOG(INFO) << "Handling INIT message from client " 
                << incoming_msg->intro().identifier() << "..." << std::endl;
        // Get client details.
        if (incoming_msg->has_intro()) {
            if (incoming_msg->manifest().og() == nsb::nsbm::Manifest::APP_CLIENT) {
                app_client_lookup.emplace(incoming_msg->intro().identifier(), ClientDetails(incoming_msg, fd_lookup));
                success = true;
            } else if (incoming_msg->manifest().og() == nsb::nsbm::Manifest::SIM_CLIENT) {
                if (cfg.SIMULATOR_MODE == Config::SimulatorMode::PER_NODE) {
                    // If per-node simulator mode, use the identifier as the key.
                    sim_client_lookup.emplace(incoming_msg->intro().identifier(), ClientDetails(incoming_msg, fd_lookup));
                    success = true;
                } else if (cfg.SIMULATOR_MODE == Config::SimulatorMode::SYSTEM_WIDE) {
                    // If system-wide simulator mode, check that there isn't already one.
                    if (sim_client_lookup.size() > 0) {
                        LOG(ERROR) << "\tSystem-wide simulator mode only allows for one simulator client." << std::endl;
                    } else {
                        // Use a generic key as it's not important.
                        sim_client_lookup.emplace("simulator", ClientDetails(incoming_msg, fd_lookup));
                        success = true;
                    }
                }
            } else {
                LOG(ERROR) << "\tUnknown/unexpected originator." << std::endl;
                return;
            }
        } else {
            LOG(ERROR) << "\tNo client details provided in INIT message." << std::endl;
            return;
        }
        *response_required = true;
        // Send back configuration details.
        nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
        out_manifest->set_op(nsb::nsbm::Manifest::INIT);
        out_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
        out_manifest->set_code(success ? nsb::nsbm::Manifest::SUCCESS : nsb::nsbm::Manifest::FAILURE);
        nsb::nsbm::ConfigParams* out_config = outgoing_msg->mutable_config();
        out_config->set_sys_mode(static_cast<nsb::nsbm::ConfigParams::SystemMode>(cfg.SYSTEM_MODE));
        out_config->set_use_db(cfg.USE_DB);
        DLOG(INFO) << "\tReturning configuration: Mode " << nsb::nsbm::ConfigParams::SystemMode(out_config->sys_mode())
                << " | Use DB? " << out_config->use_db() << std::endl;
        if (cfg.USE_DB) {
            out_config->set_db_address(cfg.DB_ADDRESS);
            out_config->set_db_port(cfg.DB_PORT);
            out_config->set_db_num(cfg.DB_NUM);
        }
        DLOG(INFO) << "\tDatabase Address: " << cfg.DB_ADDRESS << " | Database Port: " << cfg.DB_PORT << std::endl;
    }

    void NSBDaemon::handle_ping(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
        nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
        out_manifest->set_op(nsb::nsbm::Manifest::PING);
        out_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
        out_manifest->set_code(nsb::nsbm::Manifest::SUCCESS);
        *response_required = true;
    }

    void NSBDaemon::handle_send(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
        LOG(INFO) << "Handling SEND message from client " 
                << incoming_msg->intro().identifier() << " in ";
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            LOG(INFO).NoPrefix() << "PULL mode..." << std::endl;
            // Parse the metadata.
            nsb::nsbm::Metadata in_metadata = incoming_msg->metadata();
            // Retrieve payload if using database, otherwise no need.
            std::string payload_obj = msg_get_payload_obj(incoming_msg);
            // Store payload.
            MessageEntry msg_entry = MessageEntry(
                in_metadata.src_id(),
                in_metadata.dest_id(),
                payload_obj,
                in_metadata.payload_size()
            );
            DLOG(INFO) << "TX entry created | " 
                << in_metadata.payload_size() << " B | src: " 
                << msg_entry.source << " | dest: " 
                << msg_entry.destination << std::endl;
            DLOG(INFO) << (cfg.USE_DB ? "\tPayload ID: ": "\tPayload: ") << msg_entry.payload_obj << std::endl;
            // Add it to the buffer.
            tx_buffer.push_back(msg_entry);
        } else if (cfg.SYSTEM_MODE == Config::SystemMode::PUSH) {
            LOG(INFO).NoPrefix() << "PUSH mode..." << std::endl;
            // Copy the incoming message to the outgoing message, replacing with SEND to FORWARD.
            outgoing_msg->Clear();
            outgoing_msg->MergeFrom(*incoming_msg);
            nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
            out_manifest->set_op(nsb::nsbm::Manifest::FORWARD);
            // Send to sim via RECV channel.
            fd_set write_fd;
            FD_ZERO(&write_fd);
            // Select the target simulator if multiple simulator clients are used, else select the first and only one.
            ClientDetails target_sim;
            if (cfg.SIMULATOR_MODE == Config::SimulatorMode::SYSTEM_WIDE) {
                // If system-wide simulator client, just get the only client in the lookup.
                target_sim = sim_client_lookup.begin()->second;
            } else if (cfg.SIMULATOR_MODE == Config::SimulatorMode::PER_NODE) {
                // If per-node simulator client, use the source ID to specify the target sim.
                target_sim = sim_client_lookup.at(incoming_msg->metadata().src_id());
            } else {
                LOG(ERROR) << "No simulator clients available to forward message." << std::endl;
                return;
            }
            FD_SET(target_sim.ch_RECV_fd, &write_fd);
            // Check if the sim RECV channel is available.
            DLOG(INFO) << "Attempting to forward message to sim RECV channel (FD:" 
                << target_sim.ch_RECV_fd << ")..." << std::endl;
            if (select(target_sim.ch_RECV_fd + 1, nullptr, &write_fd, nullptr, nullptr) > 0) {
                if (FD_ISSET(target_sim.ch_RECV_fd, &write_fd)) {
                    // Serialize the message and send it to the sim RECV channel.
                    std::size_t size = outgoing_msg->ByteSizeLong();
                    void* buffer = malloc(size);
                    outgoing_msg->SerializeToArray(buffer, size);
                    send(target_sim.ch_RECV_fd, buffer, size, 0);
                    DLOG(INFO) << "\tForwarded message to sim RECV channel (" << size << " B)" << std::endl;
                    free(buffer);
                }
            } else {
                DLOG(ERROR) << "Sim RECV channel not available for forwarding." << std::endl; 
            }
        }
    }

    void NSBDaemon::handle_fetch(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
        LOG(INFO) << "Handling FETCH message from client " << incoming_msg->intro().identifier() << std::endl;
        MessageEntry fetched_message;
        bool tried_to_fetch = false;
        // Check to see if source has been specified.
        if (incoming_msg->has_metadata()) {
            nsb::nsbm::Metadata in_metadata = incoming_msg->metadata();
            if (in_metadata.has_src_id()) {
                // Indicate that fetch has been attempted.
                tried_to_fetch = true;
                // Search for the message in the buffer.
                for (const auto& msg : tx_buffer) {
                    if (msg.source == in_metadata.src_id()) {
                        fetched_message = msg;
                        break;
                    }
                }
            }
        }
        // If source not specified, pop the next message in the queue.
        if (!tried_to_fetch) {
            if (!tx_buffer.empty()) {
                tried_to_fetch = true;
                fetched_message = tx_buffer.front();
                tx_buffer.pop_front();
            }
        }
        DLOG(INFO) << "TX entry retrieved | " 
                << fetched_message.payload_size << " B | src: " 
                << fetched_message.source << " | dest: " 
                << fetched_message.destination << std::endl;
        DLOG(INFO) << "\tPayload: " << fetched_message.payload_obj << std::endl;
        // Prepare response.
        nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
        out_manifest->set_op(nsb::nsbm::Manifest::FETCH);
        out_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
        // If message was found (MessageEntry populated), reply with message.
        if (fetched_message.source != "") {
            out_manifest->set_code(nsb::nsbm::Manifest::MESSAGE);
            nsb::nsbm::Metadata* out_metadata = outgoing_msg->mutable_metadata();
            out_metadata->set_src_id(fetched_message.source);
            out_metadata->set_dest_id(fetched_message.destination);
            out_metadata->set_payload_size(static_cast<int>(fetched_message.payload_size));
            msg_set_payload_obj(fetched_message.payload_obj, outgoing_msg);
        } else {
            // Otherwise, indicate no message was fetched.
            out_manifest->set_code(nsb::nsbm::Manifest::NO_MESSAGE);
        }
        *response_required = true;
    }

    void NSBDaemon::handle_post(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
        LOG(INFO) << "Handling POST message from client " 
                << incoming_msg->intro().identifier() << " in ";
        if (cfg.SYSTEM_MODE == Config::SystemMode::PULL) {
            LOG(INFO).NoPrefix() << "PULL mode..." << std::endl;
            // Check for message.
            nsb::nsbm::Manifest in_manifest = incoming_msg->manifest();
            if (in_manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
                // Parse the metadata.
                nsb::nsbm::Metadata in_metadata = incoming_msg->metadata();
                // Retrieve payload if using database, otherwise no need.
                std::string payload_obj = msg_get_payload_obj(incoming_msg);
                // Store payload.
                MessageEntry msg_entry = MessageEntry(
                    in_metadata.src_id(),
                    in_metadata.dest_id(),
                    payload_obj,
                    in_metadata.payload_size()
                );
                DLOG(INFO) << "RX entry created | " 
                        << in_metadata.payload_size() << " B | src: " 
                        << msg_entry.source << " | dest: " 
                        << msg_entry.destination << "\n\tPayload: " 
                        << msg_entry.payload_obj << std::endl;
                rx_buffer.push_back(msg_entry);
            }
        } else if (cfg.SYSTEM_MODE == Config::SystemMode::PUSH) {
            LOG(INFO).NoPrefix() << "PUSH mode..." << std::endl;
            // Copy the incoming message to the outgoing message, replacing with SEND to FORWARD.
            outgoing_msg->Clear();
            outgoing_msg->MergeFrom(*incoming_msg);
            nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
            out_manifest->set_op(nsb::nsbm::Manifest::FORWARD);
            // Get the destination to forward to.
            std::string dest_id = incoming_msg->metadata().dest_id();
            int target_fd = (app_client_lookup.find(dest_id) != app_client_lookup.end()) ? app_client_lookup[dest_id].ch_RECV_fd : -1;
            // Send to sim via RECV channel.
            if (target_fd != -1) {
                fd_set write_fd;
                FD_ZERO(&write_fd);
                FD_SET(target_fd, &write_fd);
                // Check if the client RECV channel is available.
                DLOG(INFO) << "Attempting to forward message to " 
                        << dest_id << " RECV channel (FD:" 
                        << target_fd << ")..." << std::endl;
                if (select(target_fd + 1, nullptr, &write_fd, nullptr, nullptr) > 0) {
                    if (FD_ISSET(target_fd, &write_fd)) {
                        // Serialize the message and send it to the target RECV channel.
                        std::size_t size = outgoing_msg->ByteSizeLong();
                        void* buffer = malloc(size);
                        outgoing_msg->SerializeToArray(buffer, size);
                        send(target_fd, buffer, size, 0);
                        DLOG(INFO) << "\tForwarded message to " 
                                << dest_id << " RECV channel (" 
                                << size << " B)" << std::endl;
                        free(buffer);
                    }
                } else {
                    DLOG(ERROR) << dest_id << " RECV channel not available for forwarding." << std::endl; 
                }
            } else {
                DLOG(ERROR) << "No destination FD found for forwarding to " 
                            << dest_id << "." << std::endl;
            }
        }
    }

    void NSBDaemon::handle_receive(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
        LOG(INFO) << "Handling RECEIVE message from client " 
                << incoming_msg->intro().identifier() << "." << std::endl;
        MessageEntry received_message;
        bool fetched = false;
        // Check for destination.
        if (incoming_msg->has_metadata()) {
            nsb::nsbm::Metadata in_metadata = incoming_msg->metadata();
            if (in_metadata.has_dest_id()) {
                fetched = true;
                // Search for the message in the buffer.
                for (const auto& msg : rx_buffer) {
                    if (msg.destination == in_metadata.dest_id()) {
                        received_message = msg;
                        break;
                    }
                }
            }
        }
        if (received_message.exists()) {
            DLOG(INFO) << "RX entry retrieved | " 
                << received_message.payload_size << " B | src: " 
                << received_message.source << " | dest: " 
                << received_message.destination << "\n\tPayload: " 
                << received_message.payload_obj << std::endl;
        }
        // Prepare response.
        nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
        out_manifest->set_op(nsb::nsbm::Manifest::RECEIVE);
        out_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
        // If message was found (MessageEntry populated), reply with message.
        if (received_message.exists()) {
            out_manifest->set_code(nsb::nsbm::Manifest::MESSAGE);
            nsb::nsbm::Metadata* out_metadata = outgoing_msg->mutable_metadata();
            out_metadata->set_src_id(received_message.source);
            out_metadata->set_dest_id(received_message.destination);
            out_metadata->set_payload_size(static_cast<int>(received_message.payload_size));
            msg_set_payload_obj(received_message.payload_obj, outgoing_msg);
        } else {
            // Otherwise, indicate no message found.
            out_manifest->set_code(nsb::nsbm::Manifest::NO_MESSAGE);
        }
        *response_required = true;
    }

    void NSBDaemon::stop() {
        // If the server is running, stop it.
        if (running) {
            running = false;
            LOG(INFO) << "NSBDaemon stopped." << std::endl;
        }
    }

    bool NSBDaemon::is_running() const {
        return running;
    }
}

/**
 * @brief Main process to run the NSB Daemon.
 * 
 * @return int 
 */
int main(int argc, char *argv[]) {
    using namespace nsb;
    // Set up logging.
    NsbLogSink log_output = NsbLogSink();
    absl::InitializeLog();
    absl::log_internal::AddLogSink(&log_output);
    // Check argument.
    if (argc != 2) {
        LOG(ERROR) << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    // Check if the provided config file exists.
    if (access(argv[1], F_OK) == -1) {
        LOG(ERROR) << "Configuration file does not exist: " << argv[1] << std::endl;
        return 1;
    }
    // Start daemon.
    LOG(INFO) << "Starting daemon...\n";
    NSBDaemon daemon = NSBDaemon(65432, argv[1]);
    daemon.start();
    daemon.stop();
    LOG(INFO) << "Exit.";
    return 0;
}