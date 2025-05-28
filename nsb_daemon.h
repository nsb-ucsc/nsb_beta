// nsb_daemon.h

#ifndef NSB_DAEMON_H
#define NSB_DAEMON_H

#include <string>
#include <list>
#include <vector>
#include <map>
#include <array>
// Thread libraries.
#include <atomic>
#include <thread>
// I/O libraries.
#include <iostream>
#include <cstdio>
#include <format>
#include <signal.h>
// Networking libraries.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
// Data.
#include <sqlite3.h>

#include "nsb.pb.h"

// Decide whether to use threads per connection (not recommended for now).
// #define NSB_USE_THREADS
// Decide whether to use database.
// #define NSB_USE_DB

int MAX_BUFFER_SIZE = 4096;

// Message storage struct.
struct MessageEntry {
    std::string source;
    std::string destination;
    const char* payload;
    // Constructors.
    MessageEntry() : source(""), destination(""), payload(nullptr) {}
    MessageEntry(std::string src, std::string dest, const char* data)
        : source(std::move(src)), destination(std::move(dest)), payload(data) {}
};

class NSBDaemon {
public:
    // static NSBDaemon* instance;
    NSBDaemon(int s_port);
    ~NSBDaemon();
    void start();
    void stop();
    bool is_running() const;
    // static void handle_signal(int sig) {
    //     if (instance) {
    //         std::cout << "\nShutting down server..." << std::endl;
    //         instance->stop();
    //     }
    // }

private:
    std::atomic<bool> running;
    int server_port;
    std::list<MessageEntry> msg_buffer;
    void handle_connection(int fd);
    void start_server(int port);
    void handle_message(int fd, std::vector<char> message);
    // Operation-specific handlers.
    void handle_ping(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    void handle_send(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    void handle_fetch(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
};

#endif // NSB_DAEMON_H