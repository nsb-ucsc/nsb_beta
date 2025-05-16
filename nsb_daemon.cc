// nsb_daemon.cc

#include "nsb_daemon.h"

NSBDaemon::NSBDaemon(int s_port) : running(false), server_port(s_port) {}

NSBDaemon::~NSBDaemon() {
    if (running) {
        stop();
    }
}

void NSBDaemon::start() {
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
    std::vector<int> client_fds;
    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        // Set clients.
        for (int client_fd : client_fds) {
            FD_SET(client_fd, &read_fds);
            max_fd = std::max(max_fd, client_fd);
        }

        timeval timeout{};
        timeout.tv_sec = 1; // 1-second timeout
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("Select error.");
            break;
        }

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
            // Add to the connection pool.
            client_fds.push_back(client_fd);
        }

        // Monitor existing connections.
        for (auto it=client_fds.begin(); it!=client_fds.end();) {
            int fd = *it;
            if (FD_ISSET(fd, &read_fds)) {
                char buffer[1024] = {0};
                int bytes_read = read(fd, buffer, sizeof(buffer)-1);
                if (bytes_read <= 0) {
                    printf("Client (FD:%d) has disconnected.\n", fd);
                    close(fd);
                    it = client_fds.erase(it);
                } else {
                    printf("Received from client (FD:%d): %s\n", fd, buffer);
                    ++it;
                }
            } else {++it;}
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