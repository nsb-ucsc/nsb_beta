// nsb_daemon.h

#ifndef NSB_DAEMON_H
#define NSB_DAEMON_H

#include <string>
#include <vector>
#include <map>
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

int MAX_BUFFER_SIZE = 4096;

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
    void handle_connection(int fd);
    void start_server(int port);
    // static void handle_signal(int sig);
    void handle_message(int fd, std::vector<char> message);
};

#endif // NSB_DAEMON_H