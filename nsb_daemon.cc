// nsb_daemon.cc

#include "nsb_daemon.h"
#include "nsb.pb.h"



NSBDaemon::NSBDaemon(int s_port) : running(false), server_port(s_port) {
    // signal(SIGINT, handle_signal);
    GOOGLE_PROTOBUF_VERIFY_VERSION;
}

NSBDaemon::~NSBDaemon() {
    if (running) {
        stop();
    }
    google::protobuf::ShutdownProtobufLibrary();
}

void NSBDaemon::start() {
    if (!running) {
        running = true;
        start_server(server_port);
        std::cout << "NSBDaemon started." << std::endl;
    }
}

void NSBDaemon::handle_connection(int fd) {
    printf("entered\n");
    fd_set conn_read_fds, conn_write_fds;
    while (running) {
        // Prepare file descriptor.
        FD_ZERO(&conn_read_fds);
        FD_ZERO(&conn_write_fds);
        FD_SET(fd, &conn_read_fds);
        FD_SET(fd, &conn_write_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        // Check for activity.
        int activity = select(fd+1, &conn_read_fds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Select error when handling connection");
                break;
            }
        } else if (FD_ISSET(fd, &conn_read_fds)) {
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
            }
            else {
                printf("Disconnected from (FD:%d)\n", fd);
                shutdown(fd, SHUT_WR);
                close(fd);
            }
        }
    }
    printf("Terminated. Disconnecting from (FD:%d).\n", fd);
    shutdown(fd, SHUT_WR);
    close(fd);
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
    // Bind and listen.
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
    // Create vector to track threads.
    std::vector<std::thread> active_threads;
    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        timeval timeout{};
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Select error.");
                break;
            }
        } else if (activity > 0) {
            // Handle new connections.
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
                // Spin off each connection as a thread.
                active_threads.push_back(std::thread(&NSBDaemon::handle_connection, this, client_fd));
            }
        }
    }
    // Clean up threads.
    for (auto it = active_threads.begin(); it != active_threads.end();) {
        if (it->joinable()) {
            it->join();
            it = active_threads.erase(it);
        } else {
            ++it;
        }
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
    // Prepare response.
    nsb::nsbm nsb_response;
    nsb::nsbm::Manifest* r_manifest = nsb_response.mutable_manifest();
    bool response_required = false;
    switch (manifest.op()) {
        case nsb::nsbm::Manifest::PING:
            printf("\t...it's a PING!\n");
            r_manifest->set_op(nsb::nsbm::Manifest::PING);
            r_manifest->set_og(nsb::nsbm::Manifest::DAEMON);
            r_manifest->set_code(nsb::nsbm::Manifest::SUCCESS);
            response_required = true;
            break;
        case nsb::nsbm::Manifest::EXIT:
            printf("\tLooks like we're done here.\n");
            stop();
            break;
        default:
            printf("\tUnknown operation.");
            // Create a negative PING response.
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

void NSBDaemon::stop() {
    if (running) {
        running = false;
        std::cout << "NSBDaemon stopped." << std::endl;
    }
}

bool NSBDaemon::is_running() const {
    return running;
}

int main() {
    std::cout << "Starting daemon...\n";
    NSBDaemon daemon = NSBDaemon(65432);
    daemon.start();
    daemon.stop();
    std::cout << "Exit.";
    return 0;
}