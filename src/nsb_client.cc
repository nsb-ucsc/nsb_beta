// nsb.cc

#include "nsb_client.h"

namespace nsb {
    SocketInterface::SocketInterface(const std::string& serverAddress, int serverPort)
        : serverAddress(serverAddress), serverPort(serverPort) {
            connectToServer(SERVER_CONNECTION_TIMEOUT);
        }

    SocketInterface::~SocketInterface() {
        closeConnection();
    }

    int SocketInterface::connectToServer(int timeout) {
        LOG(INFO) << "Connecting to daemon@" << serverAddress << ":" << serverPort << "..." << std::endl;
        // Set the target time.
        std::chrono::time_point startTime = std::chrono::system_clock::now();
        std::chrono::time_point targetTime = startTime + std::chrono::seconds(timeout);
        // For each channel, try to configure and connect sockets.
        for (Channel channel : Channels) {
            LOG(INFO) << "Configuring & connecting " << getChannelName(channel) << "..." << std::endl;
            while (std::chrono::system_clock::now() < targetTime) {
                // Try configuring and connecting to the daemon server.
                int conn = socket(AF_INET, SOCK_STREAM, 0);
                if (conn < 0) {
                    LOG(ERROR) << "\tSocket creation failed." << std::endl;
                    return -1;
                }
                // Configure socket with options for low latency.
                int opt = 1;
                if (setsockopt(conn, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                    LOG(ERROR) << "\tCould not set socket option SOL_SOCKET to SO_REUSEADDR." << std::endl;
                    close(conn);
                    return -1;
                }
                if (setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
                    LOG(ERROR) << "\tCould not set socket option IPPROTO_TCP to TCP_NODELAY." << std::endl;
                    close(conn);
                    return -1;
                }
                if (setsockopt(conn, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
                    LOG(ERROR) << "\tCould not set socket option SOL_SOCKET to SO_KEEPALIVE." << std::endl;
                    close(conn);
                    return -1;
                }
                // Attempt to connect.
                sockaddr_in serverAddrDetails{};
                serverAddrDetails.sin_family = AF_INET;
                serverAddrDetails.sin_addr.s_addr = inet_addr(serverAddress.c_str());
                serverAddrDetails.sin_port = htons(serverPort);
                if (connect(conn, (struct sockaddr*)&serverAddrDetails, sizeof(serverAddrDetails)) == -1) {
                    LOG(ERROR) << "\tRetrying connection..." << std::endl;
                    close(conn);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } else {
                    LOG(INFO) << "\tConnected!" << std::endl;
                    conns.insert({channel, conn});
                    break;
                }
            }
            // If loop broken due to timeout, that's a problem.
            if (std::chrono::system_clock::now() > targetTime) {
                LOG(ERROR) << "Connection to server timed out after " << timeout << " seconds." << std::endl;
                return -1;
            }
        }
        // Set all connections to non-blocking after setup, to ensure that they've connected.
        for (Channel channel : Channels) {
            DLOG(INFO) << "Setting " << getChannelName(channel) << " to non-blocking..." << std::endl;
            int flags = fcntl(conns.at(channel), F_GETFL, 0);
            if (flags == -1) {
                LOG(ERROR) << "\tFailed to get flags for socket." << std::endl;
                return -1;
            }
            if (fcntl(conns.at(channel), F_SETFL, flags | O_NONBLOCK) == -1) {
                LOG(ERROR) << "\tFailed to set non-blocking mode for socket." << std::endl;
                return -1;
            }
        }
        LOG(INFO) << "All channels connected!" << std::endl;
        return 0;
    }

    void SocketInterface::closeConnection() {
        for (Channel channel : Channels) {
            shutdown(conns.at(channel), SHUT_WR);
            close(conns.at(channel));
        }
    }
}

int main() {
    using namespace nsb;
    // Set up logging.
    NsbLogSink log_output = NsbLogSink();
    absl::InitializeLog();
    absl::log_internal::AddLogSink(&log_output);
    // Testing
    LOG(INFO) << "Creating socket interface..." << std::endl;
    SocketInterface sif = SocketInterface("127.0.0.1", 65432);
    LOG(INFO) << "Disconnecting socket interace..." << std::endl;
    sif.closeConnection();
    LOG(INFO) << "Done!" << std::endl;
}