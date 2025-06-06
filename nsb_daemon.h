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
#include <iomanip>
#include <chrono>
#include <cstdio>
#include <format>
#include <signal.h>
// Networking libraries.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
// Data, configuration, and logging.
#include <sqlite3.h>
#include "nsb.pb.h"
#include <yaml-cpp/yaml.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <absl/log/log.h>
#include <absl/log/check.h>
#include <absl/log/log_sink.h>
#include <absl/log/log_entry.h>
#include <absl/log/internal/log_sink_set.h>
#include <absl/time/time.h>
#include <absl/time/civil_time.h>

/** @brief The maximum buffer size for sending and receiving messages. */
int MAX_BUFFER_SIZE = 4096;

/**
 * @brief Configuration parameters struct.
 * 
 * This struct contains the configuration parameters loaded from the 
 * configuration file.
 */
struct Config {
    enum class SystemMode {
        PULL = 0,
        PUSH = 1
    };
    SystemMode SYSTEM_MODE;
    bool USE_DB;
    std::string DB_ADDRESS;
    int DB_PORT;
};

class NSBDaemon {
public:
    /**
     * @brief Construct a new NSBDaemon::NSBDaemon object.
     * 
     * This method initializes attributes and verifies the Protobuf version.
     * 
     * @param s_port The port that NSB clients will connect to.
     * @param filename The path to the YAML configuration file.
     */
    NSBDaemon(int s_port, std::string filename);
    /**
     * @brief Destroy the NSBDaemon::NSBDaemon object.
     * 
     * This method will check to see if the server is still running and stop it if 
     * necessary. It will then shut down the Protobuf library.
     */
    ~NSBDaemon();
    /**
     * @brief Start the NSB Daemon.
     * 
     * This method will launch the server at the server port using the start_server 
     * method.
     * 
     * @see start_server(int port)
     */
    void start();
    /**
     * @brief Stops the NSB Daemon.
     * 
     * Checks if server is running and stops it, resulting in the daemon shutting 
     * down.
     */
    void stop();
    /**
     * @brief Checks if the server is running.
     * 
     * @return true if the server is running, false otherwise.
     * @return false if the server is not running.
     */
    bool is_running() const;

private:
    /**
     * @brief Client details struct.
     * 
     * This struct contains address/port information for each client that connects. 
     * This doesn't have immediate use but may be useful in the future.
     * 
     */
    struct ClientDetails {
        std::string identifier;
        std::string address;
        int ch_CTRL_port;
        int ch_CTRL_fd;
        int ch_SEND_port;
        int ch_SEND_fd;
        int ch_RECV_port;
        int ch_RECV_fd;
        ClientDetails() : address(""), ch_CTRL_port(0), ch_CTRL_fd(-1), ch_SEND_port(0),
                        ch_SEND_fd(-1), ch_RECV_port(0), ch_RECV_fd(-1) {}
        ClientDetails(nsb::nsbm* nsb_msg, std::map<std::string, int> fd_lookup) {
            identifier = nsb_msg->intro().identifier();
            address = nsb_msg->intro().address();
            ch_CTRL_port = nsb_msg->intro().ch_ctrl();
            ch_SEND_port = nsb_msg->intro().ch_send();
            ch_RECV_port = nsb_msg->intro().ch_recv();
            // Populate the file descriptors.
            std::string ctrl_addr = address + ":" + std::to_string(ch_CTRL_port);
            ch_CTRL_fd = fd_lookup.find(ctrl_addr) != fd_lookup.end() ?
                        fd_lookup[ctrl_addr] : -1;
            std::string send_addr = address + ":" + std::to_string(ch_SEND_port);
            ch_SEND_fd = fd_lookup.find(send_addr) != fd_lookup.end() ?
                        fd_lookup[send_addr] : -1;
            std::string recv_addr = address + ":" + std::to_string(ch_RECV_port);
            ch_RECV_fd = fd_lookup.find(recv_addr) != fd_lookup.end() ?
                        fd_lookup[recv_addr] : -1;
        
        }
    };

    /**
     * @brief Message storage struct.
     * 
     * This struct contains source and destination information and the payload and 
     * is intended to be used to store messages in the daemon's transmission and 
     * reception buffers.
     * 
     */
    struct MessageEntry {
        /** @brief The source identifier. */
        std::string source;
        /** @brief The destination identifier. */
        std::string destination;
        /** @brief The payload as a bytestring, but in const char* form. */
        std::string payload;
        // Constructors.
        /** @brief Blank constructor. */
        MessageEntry() : source(""), destination(""), payload("") {}
        /** @brief Populated constructor. */
        MessageEntry(std::string src, std::string dest, std::string data)
            : source(std::move(src)), destination(std::move(dest)), payload(std::move(data)) {}
    };
    /** @brief Configuration object. */
    Config cfg;
    /** @brief A flag set to indicate daemon server status. */
    std::atomic<bool> running;
    /** @brief The server port accessible to client connections. */
    int server_port;
    /** @brief Details of the simulator client. */
    ClientDetails sim;
    /** @brief A mapping of client identifiers to their details. */
    std::map<std::string, ClientDetails> client_lookup;
    /** @brief A mapping of "address:port" strings to their file descriptors. */
    std::map<std::string, int> fd_lookup;
    /**
     * @brief Transmission buffer to store sent payloads waiting to be fetched.
     * 
     * @see MessageEntry
     * @see handle_send()
     * @see handle_fetch()
     */
    std::list<MessageEntry> tx_buffer;
    /**
     * @brief Reception buffer to store posted payloads waiting to be received.
     * 
     * @see MessageEntry
     * @see handle_post()
     * @see handle_receive()
     */
    std::list<MessageEntry> rx_buffer;
     /**
     * @brief Configure the daemon with a YAML file.
     * 
     * This method uses the passed-in YAML file to set parameters. Some or all
     * of these parameters will be passed to clients that connect through the 
     * INIT message transaction.
     * 
     * @see handle_init()
     */
    void configure(std::string filename);
    /**
     * @brief Start the socket-connected server within the NSB Daemon.
     * 
     * This is the main servicing method that runs for the lifetime of the NSB 
     * Daemon. It opens a multiple connection-enabled server and maintains 
     * persistent connections as communication channels with each NSB client that 
     * connects to it. New connections are managed through an updating vector of 
     * file descriptors where each represents a different connection. When messages 
     * come in from existing connections, they will be passed onto the 
     * handle_message method.
     * 
     * This method is invoked by the start() method.
     * 
     * @param port The port that will be accessible for clients to connect.
     * 
     * @see start()
     * @see handle_message()
     */
    void start_server(int port);
    /**
     * @brief A multiplexer to parse messages and redirect them to handlers.
     * 
     * This method is invoked by the server (in the start_server() method) to handle 
     * an incoming message. It parses the message using Protobuf, and then redirects 
     * the incoming message and a template outgoing message (in case a response is 
     * necessary) to one of the operation-specific handlers.
     * 
     * If the operation is not understood, the server will respond with a negative 
     * PING message.
     * 
     * @param fd The file descriptor of the client connection.
     * @param message The incoming message to parse and handle.
     * 
     * @see start_server()
     * @see handle_ping()
     * @see handle_send()
     * @see handle_fetch()
     * @see handle_post()
     * @see handle_receive()
     */
    void handle_message(int fd, std::vector<char> message);

    /* Operation-specific handlers. */

    /**
     * @brief Handles INIT messages.
     * 
     * When the server receives an INIT message from a client, it will register
     * the identifier of the client, its address, and its different channel port 
     * information. In response, it will pass on the configuration parameters to
     * the client so that they can be inherited across the NSB system.
     */
    void handle_init(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    /**
     * @brief Handles PING messages.
     * 
     * Since the PING has been received, it can be assumed to be successful. As such 
     * this method populates the outgoing message as an NSB PING message indicating 
     * success.
     * 
     * @param incoming_msg The incoming message that is being handled.
     * @param outgoing_msg A template message that can be used if a response is 
     *                     required.
     * @param response_required Whether or not a response is required and the 
     *                          outgoing message will be sent back to the client.
     */
    void handle_ping(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    /**
     * @brief Handles SEND messages from the NSB Application Client.
     * 
     * This method handles SEND messages by parsing the incoming message and storing 
     * the source, destination, and payload as a MessageEntry. The new MessageEntry 
     * will be pushed back in the transmission buffer where it will be ready to be 
     * fetched by the NSB Simulator Client.
     * 
     * @param incoming_msg The incoming message that is being handled.
     * @param outgoing_msg A template message that can be used if a response is 
     *                     required.
     * @param response_required Whether or not a response is required and the 
     *                          outgoing message will be sent back to the client.
     * 
     * @see MessageEntry
     * @see handle_fetch()
     */
    void handle_send(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    /**
     * @brief Handles FETCH messages from the NSB Simulator Client.
     * 
     * This method first creates a blank MessageEntry. If a source has been 
     * specified, it will search the transmission buffer for a message with that 
     * source, either setting the blank MessageEntry to the found entry if the query 
     * was resolved or leaving it blank if not found. If a source has not been 
     * specified, the top MessageEntry of the buffer will be popped off and used; 
     * otherwise, if the buffer is empty, the MessageEntry will be left blank.
     * 
     * If a message was found, a NSB FETCH message indicating MESSAGE will be sent 
     * with the metadata and payload. Otherwise, a NSB FETCH message indicating 
     * NO_MESSAGE will be sent back to the client.
     * 
     * @param incoming_msg The incoming message that is being handled.
     * @param outgoing_msg A template message that can be used if a response is 
     *                     required.
     * @param response_required Whether or not a response is required and the 
     *                          outgoing message will be sent back to the client.
     * 
     * @see MessageEntry
     */
    void handle_fetch(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    /**
     * @brief Handles POST messages from the NSB Simulator Client.
     * 
     * This method handles POST messages by parsing the incoming message and storing 
     * the source, destination, and payload as a MessageEntry. The new MessageEntry 
     * will be pushed back in the reception buffer where it will be ready to be 
     * received by the NSB Application Client.
     * 
     * @param incoming_msg The incoming message that is being handled.
     * @param outgoing_msg A template message that can be used if a response is 
     *                     required.
     * @param response_required Whether or not a response is required and the 
     *                          outgoing message will be sent back to the client.
     * 
     * @see MessageEntry
     * @see handle_receive()
     */
    void handle_post(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
    /**
     * @brief Handles RECEIVE messages from the NSB Application Client.
     * 
     * This method first creates a blank MessageEntry. If a destination has been 
     * specified, it will search the reception buffer for a message with that 
     * destination, either setting the blank MessageEntry to the found entry if the 
     * query was resolved or leaving it blank if not found. If a destination has not 
     * been specified, the top MessageEntry of the buffer will be popped off and 
     * used; otherwise, if the buffer is empty, the MessageEntry will be left blank.
     * 
     * If a message was found, a NSB RECEIVE message indicating MESSAGE will be sent 
     * with the metadata and payload. Otherwise, a NSB RECEIVE message indicating 
     * NO_MESSAGE will be sent back to the client.
     * 
     * @param incoming_msg The incoming message that is being handled.
     * @param outgoing_msg A template message that can be used if a response is 
     *                     required.
     * @param response_required Whether or not a response is required and the 
     *                          outgoing message will be sent back to the client.
     * 
     * @see MessageEntry
     */
    void handle_receive(nsb::nsbm* incoming_msg, nsb::nsbm* outgoing_msg, bool* response_required);
};

class NsbLogSink : public absl::LogSink {
    public:
        void Send(const absl::LogEntry& entry) override {
            // Get microseconds.
            absl::Time ts = entry.timestamp();
            absl::TimeZone tz = absl::LocalTimeZone();
            absl::CivilSecond civ_sec = absl::ToCivilSecond(ts, tz);

            int64_t ms = absl::ToInt64Microseconds(ts - absl::FromCivil(civ_sec, tz));

            // Set severity.
            std::string severity;
            switch (entry.log_severity()) {
                case absl::LogSeverity::kInfo:      severity = "(info)"; break;
                case absl::LogSeverity::kWarning:   severity = "(warning)"; break;
                case absl::LogSeverity::kError:     severity = "(error)"; break;
                case absl::LogSeverity::kFatal:     severity = "(FATAL)"; break;
                default:                            severity = "(other)"; break;
            }

            // Stream message.
            std::cout << "[" << std::setw(2) << civ_sec.hour() << ":" << std::setw(2) << civ_sec.minute() << ":" 
                      << std::setw(2) << civ_sec.second() << "." << std::setw(6) << ms << "] "
                      << std::setw(9) << severity << " " << entry.text_message();
        }
};

#endif // NSB_DAEMON_H