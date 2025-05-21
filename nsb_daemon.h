// nsb_daemon.h

#ifndef NSB_DAEMON_H
#define NSB_DAEMON_H

// I/O libraries.
#include <iostream>
#include <cstdio>
#include <string>
#include <format>
// Networking libraries.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <fcntl.h>

class NSBDaemon {
public:
    NSBDaemon(int s_port);
    ~NSBDaemon();
    void start();
    void stop();
    bool is_running() const;

private:
    bool running;
    int server_port;
    void start_server(int port);
    // int handle_message(int fd, int fd, nsb::nsbm nsb_msg);
};

#endif // NSB_DAEMON_H