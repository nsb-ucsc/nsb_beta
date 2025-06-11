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

    int SocketInterface::sendMessage(Comms::Channel channel, const std::string& message) {
        int totalBytesSent = 0;
        int totalSize = message.size();
        while (totalBytesSent < totalSize) {
            int bytesSent = send(conns.at(channel), message.data() + totalBytesSent, totalSize - totalBytesSent, 0);
            if (bytesSent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // May not be read to send yet.
                    continue;
                } else {
                    LOG(ERROR) << "Failed to send message on " << getChannelName(channel) << ": " << strerror(errno) << std::endl;
                    return -1;
                }
            }
            totalBytesSent += bytesSent;
        }
        return 0;
    }

    std::string SocketInterface::receiveMessage(Comms::Channel channel, int* timeout) {
        int* fdPtr = &conns.at(channel);
        // Set up FD for select.
        fd_set readFDs;
        FD_ZERO(&readFDs);
        FD_SET(*fdPtr, &readFDs);
        // Set up data stores.
        std::string message;
        char buffer[RECEIVE_BUFFER_SIZE];
        bool messageExists = false;
        // Wait for messages.
        timeval* t;
        if (timeout == nullptr) {
            t = nullptr;
        } else {
            timeval timeoutVal{};
            timeoutVal.tv_sec = *timeout;
            timeoutVal.tv_usec = 0;
            t = &timeoutVal;
        }
        while (true) {
            int activity = select(*fdPtr + 1, &readFDs, nullptr, nullptr, t);
            if (activity < 0) {
                LOG(ERROR) << "Select error: " << strerror(errno) << std::endl;
                return std::string();
            } else if (activity == 0) {
                LOG(WARNING) << "Timeout waiting for message on " << getChannelName(channel) << "." << std::endl;
                return std::string();
            } else {
                // Read buffer until there's nothing left.
                int bytesRead = recv(*fdPtr, buffer, RECEIVE_BUFFER_SIZE-1, 0);
                while (bytesRead > 0) {
                    messageExists = true;
                    message.append(buffer, bytesRead);
                    bytesRead = recv(*fdPtr, buffer, RECEIVE_BUFFER_SIZE-1, 0);
                }
                if (messageExists) {
                    return message;
                }
            }
        }
        return std::string();
    }

    std::future<std::string> SocketInterface::listenForMessage(Comms::Channel channel, int* timeout) {
        return std::async(std::launch::async, [this, channel, timeout]() {
            return receiveMessage(channel, timeout);
        });
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
}