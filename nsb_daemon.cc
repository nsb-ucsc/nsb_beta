// nsb_daemon.cc

#include "nsb_daemon.h"

NSBDaemon::NSBDaemon(int s_port) : running(false), server_port(s_port) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
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
        std::cout << "NSBDaemon started." << std::endl;
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
        perror(error_msg.c_str());
        close(server_fd);
        return;
    }
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("Listen failed.");
        close(server_fd);
        return;
    }
    std::cout << "Server started on port " << port << std::endl;

    // Run server.
    fd_set read_fds;
    // Create vector to track client file descriptors.
    std::vector<int> client_fds;
    while (running) {
        // Set server file descriptor.
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;
        // Set client file descriptors.
        for (int client_fd : client_fds) {
            FD_SET(client_fd, &read_fds);
            max_fd = std::max(max_fd, client_fd);
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
            for (auto it=client_fds.begin(); it!=client_fds.end();) {
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
                        printf("Picked up %d bytes from (FD:%d)\n", bytes_read, fd);
                        message.insert(message.end(), buffer, buffer+bytes_read);
                        bytes_read = recv(fd, buffer, sizeof(buffer)-1, 0);
                    }
                    if (message_exists) {
                        printf("Received message from (FD:%d): %s\n", fd, message.data());
                        handle_message(fd, message);
                        ++it;
                    }
                    else {
                        printf("Disconnected from (FD:%d)\n", fd);
                        shutdown(fd, SHUT_WR);
                        close(fd);
                        client_fds.erase(it);
                    }
                }
                else {++it;}
            }
            // Then handle any new connections.
            if (FD_ISSET(server_fd, &read_fds)) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd == -1) {
                    perror("Accept failed.");
                    continue;
                }
                char addr_string[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), addr_string, INET_ADDRSTRLEN);
                printf("New connection accepted: %s\n", addr_string);
                // Add client FD to the pool of client FDs.
                client_fds.push_back(client_fd);
            }
        }
    }
    // When running stops, close connections and close server.
    for (int client_fd : client_fds) {
        printf("Closing connection to %d\n", client_fd);
        close(client_fd);
    }
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
}

void NSBDaemon::handle_message(int fd, std::vector<char> message) {
    nsb::nsbm nsb_message;
    nsb_message.ParseFromArray(message.data(), message.size());
    nsb::nsbm::Manifest manifest = nsb_message.manifest();
    printf("Manifest %d-%d-%d received from %d\n",
        manifest.op(), manifest.og(), manifest.code(), fd);
    // Get message fields.
    nsb::nsbm::Metadata metadata = nsb_message.metadata();
    // Prepare template for response.
    nsb::nsbm nsb_response;
    nsb::nsbm::Manifest* r_manifest = nsb_response.mutable_manifest();
    bool response_required = false;
    // Redirect handling based on specified operation.
    switch (manifest.op()) {
        case nsb::nsbm::Manifest::PING:
            printf("\t...it's a PING!\n");
            handle_ping(&nsb_message, &nsb_response, &response_required);
            break;
        case nsb::nsbm::Manifest::SEND:
            printf("\tPackage-to-send received:\n");
            handle_send(&nsb_message, &nsb_response, &response_required);
            break;
        case nsb::nsbm::Manifest::FETCH:
            printf("\tGrabbing the payload.\n");
            handle_fetch(&nsb_message, &nsb_response, &response_required);
            break;
        case nsb::nsbm::Manifest::POST:
            printf("\tPosting delivery (or not).\n");
            handle_post(&nsb_message, &nsb_response, &response_required);
            break;
        case nsb::nsbm::Manifest::RECEIVE:
            printf("\tPolling for payload.\n");
            handle_receive(&nsb_message, &nsb_response, &response_required);
            break;
        case nsb::nsbm::Manifest::EXIT:
            printf("\tLooks like we're done here.\n");
            // Stop the daemon.
            stop();
            break;
        default:
            printf("\tUnknown operation.\n");
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
        printf("\tBack at ya! (%dB) %s\n", size, r_buffer);
        send(fd, r_buffer, size, 0);
    }
}

void NSBDaemon::handle_ping(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
    nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
    out_manifest->set_op(nsb::nsbm::Manifest::PING);
    out_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
    out_manifest->set_code(nsb::nsbm::Manifest::SUCCESS);
    *response_required = true;
}

void NSBDaemon::handle_send(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
    // Parse the metadata.
    nsb::nsbm::Metadata in_metadata = incoming_msg->metadata();
    // Store payload.
    MessageEntry msg_entry = MessageEntry(
        in_metadata.src_id(),
        in_metadata.dest_id(),
        incoming_msg->payload()
    );
    printf("\tTX entry created | %d B | src: %s | dest: %s\n\tPayload: %s\n",
        in_metadata.payload_size(), msg_entry.source.c_str(),
        msg_entry.destination.c_str(), msg_entry.payload.c_str());
    // Add it to the buffer.
    tx_buffer.push_back(msg_entry);
}

void NSBDaemon::handle_fetch(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
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
    printf("\tTX entry retrieved | %d B | src: %s | dest: %s\n\tPayload: %s\n",
        strlen(fetched_message.payload.c_str()),
        fetched_message.source.c_str(),
        fetched_message.destination.c_str(),
        fetched_message.payload.c_str());
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
        out_metadata->set_payload_size(strlen(fetched_message.payload.c_str()));
        outgoing_msg->set_payload(fetched_message.payload);
    } else {
        // Otherwise, indicate no message was fetched.
        out_manifest->set_code(nsb::nsbm::Manifest::NO_MESSAGE);
    }
    *response_required = true;
}

void NSBDaemon::handle_post(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
    // Check for message.
    nsb::nsbm::Manifest in_manifest = incoming_msg->manifest();
    if (in_manifest.code() == nsb::nsbm::Manifest::MESSAGE) {
        // Parse the metadata.
        nsb::nsbm::Metadata in_metadata = incoming_msg->metadata();
        // Store payload.
        MessageEntry msg_entry = MessageEntry(
            in_metadata.src_id(),
            in_metadata.dest_id(),
            incoming_msg->payload()
        );
        printf("\tRX entry created | %d B | src: %s | dest: %s\n\tPayload: %s\n",
            in_metadata.payload_size(), msg_entry.source.c_str(), msg_entry.destination.c_str(), msg_entry.payload.c_str());
        rx_buffer.push_back(msg_entry);
    }
}

void NSBDaemon::handle_receive(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required) {
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
    // Pop the next message in the queue.
    if (!fetched) {
        if (!rx_buffer.empty()) {
            received_message = tx_buffer.front();
            rx_buffer.pop_front();
            fetched = true;
        }
    }
    printf("\tRX entry retrieved | %d B | src: %s | dest: %s\n\tPayload: %s\n",
        strlen(received_message.payload.c_str()),
        received_message.source.c_str(),
        received_message.destination.c_str(),
        received_message.payload.c_str());
    // Prepare response.
    nsb::nsbm::Manifest* out_manifest = outgoing_msg->mutable_manifest();
    out_manifest->set_op(nsb::nsbm::Manifest::RECEIVE);
    out_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
    // If message was found (MessageEntry populated), reply with message.
    if (received_message.source != "") {
        out_manifest->set_code(nsb::nsbm::Manifest::MESSAGE);
        nsb::nsbm::Metadata* out_metadata = outgoing_msg->mutable_metadata();
        out_metadata->set_src_id(received_message.source);
        out_metadata->set_dest_id(received_message.destination);
        out_metadata->set_payload_size(strlen(received_message.payload.c_str()));
        outgoing_msg->set_payload(received_message.payload);
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
        std::cout << "NSBDaemon stopped." << std::endl;
    }
}

bool NSBDaemon::is_running() const {
    return running;
}

/**
 * @brief Main process to run the NSB Daemon.
 * 
 * @return int 
 */
int main() {
    std::cout << "Starting daemon...\n";
    NSBDaemon daemon = NSBDaemon(65432);
    daemon.start();
    daemon.stop();
    std::cout << "Exit.";
    return 0;
}