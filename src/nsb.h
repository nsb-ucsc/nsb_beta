// nsb.h

#ifndef NSB_H
#define NSB_H

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
#include <future>
// Networking libraries.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
// Data, configuration, and logging.
#include <sqlite3.h>
#include "proto/nsb.pb.h"
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
// Database.
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#define SERVER_CONNECTION_TIMEOUT 10
#define DAEMON_RESPONSE_TIMEOUT 600
#define RECEIVE_BUFFER_SIZE 4096
#define SEND_BUFFER_SIZE 4096

namespace nsb {

    /** @brief Log sink definition to be used with Abseil logging. */
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
                std::cout << "[" << std::setfill('0') << std::setw(2)
                    << civ_sec.hour() << ":" << std::setw(2) << civ_sec.minute() << ":" 
                    << std::setw(2) << civ_sec.second() << "." << std::setw(6) << ms << "] "
                    << std::setfill(' ') << std::setw(9) << severity << " " << entry.text_message();
            }
    };

    /**
     * @brief Configuration parameters struct.
     * 
     * This struct contains the configuration parameters loaded from the 
     * configuration file. The included property codes for SystemMode should be 
     * standardized across Python and C++ libraries.
     */
    struct Config {
        /**
         * @brief Denotes whether the NSB system is in *PUSH* mode or *PULL* 
         * mode.
         * 
         * *PULL* mode requires clients to request -- or pull -- to fetch or 
         * receive incoming payloads via the daemon server's response. *PUSH* 
         * mode denotes that when clients send or post outgoing payloads, they 
         * are immediately forwarded to the appropriate client.
         * 
         * @see NSBAppClient::receive()
         * @see NSBSimClient::fetch()
         */
        enum class SystemMode {
            PULL = 0,
            PUSH = 1
        };
        SystemMode SYSTEM_MODE;
        bool USE_DB;
        std::string DB_ADDRESS;
        int DB_PORT;
        int DB_NUM;

        /**  @brief Blank constructor for a new Config object. */
        Config() : SYSTEM_MODE(SystemMode::PULL), USE_DB(false), DB_ADDRESS(""), DB_PORT(0), DB_NUM(0) {}
        /** @brief Constructor for a new Config object using NSB message. */
        Config(nsb::nsbm msg) {
            nsb::nsbm::ConfigParams cfg = msg.config();
            SYSTEM_MODE = SystemMode(cfg.sys_mode());
            USE_DB = cfg.use_db();
            if (USE_DB) {
                DB_ADDRESS = cfg.db_address();
                DB_PORT = cfg.db_port();
                DB_NUM = cfg.db_num();
            }
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
        /** @brief The payload or payload key (if using database). */
        std::string payload_obj;
        /** @brief The size of the payload. */
        int payload_size;
        // Constructors.
        /** @brief Blank constructor. */
        MessageEntry() : source(""), destination(""), payload_obj(""), payload_size(0) {}
        /** @brief Populated constructor. */
        MessageEntry(std::string src, std::string dest, std::string data, int size)
            : source(std::move(src)), destination(std::move(dest)),
              payload_obj(std::move(data)), payload_size(size){}
    };

    /**
     * @brief Base class for communication interfaces.
     * 
     * As NSB is expected to support different communication paradigms and 
     * protocols, we plan to provide various different communication interfaces 
     * with the same basic functions. The SocketInterface class should be used 
     * as an example to develop other interfaces.
     */
    class Comms {
    public:
        /** @brief Shared enumeration to designate different channels. */
        enum class Channel {
            CTRL = 0,
            SEND = 1,
            RECV = 2
        };
        const std::vector<Channel> Channels = {Channel::CTRL, Channel::SEND, Channel::RECV};
        /**
         * @brief Helper function to get channel name with enumerated value.
         * 
         * @param channel The channel enumeration.
         * @return std::string The name of the channel.
         */
        std::string getChannelName(Channel channel) {
            return ChannelName.at(channel);
        }
    private:
        const std::map<Channel, std::string> ChannelName = {
            {Channel::CTRL, "CTRL"},
            {Channel::SEND, "SEND"},
            {Channel::RECV, "RECV"}
        };
    };

    /**
     * @brief Socket interface for client-server communication.
     * 
     * This class implements socket interfacing to facilitate network 
     * communication between NSB clients and the daemon server. This can be used
     * as a template to develop other interfaces for client communication, which
     * must define the same methods with the same arguments as done in this
     * class.
     */
    class SocketInterface : public Comms {
    public:
        /**
         * @brief Constructor for a new SocketInterface object.
         * 
         * Sets the address and port of the server at the NSB daemon before 
         * connecting to it.
         * 
         * @param server_address 
         * @param server_port
         * 
         * @see SocketInterface::connectToServer()
         */
        SocketInterface(std::string server_address, int server_port);
        /**
         * @brief Deconstructor for SocketInterface object.
         * 
         * Closes the connection before destruction.
         */
        ~SocketInterface();
        /**
         * @brief Connects to the daemon with the stored server address and 
         * port.
         * 
         * This method configures and connects sockets for each of the client's 
         * channels and then attempts to connect to the daemon.
         * 
         * @param timeout Maximum time in seconds to wait to connect to the 
         *                daemon.
         * @return int Returns 0 if connection was successful, else -1.
         */
        int connectToServer(int timeout);
        /**
         * @brief Closes the socket connection.
         * 
         * Attempts to shutdown the socket, then closes it.
         */
        void closeConnection();
        /**
         * @brief Sends a message to the server.
         * 
         * This method sends a message over the specified channel to the server.
         * 
         * @param channel The channel to send the message on (CTRL, SEND, or 
         *                RECV).
         * @param message The message to send to the server.
         * @return int Returns 0 if send is successful, else -1.
         */
        int sendMessage(Comms::Channel channel, const std::string& message);
        /**
         * @brief Receives a message from the server.
         * 
         * This method uses selectors to wait for the channel socket to be ready
         * before receiving up to RECEIVE_BUFFER_SIZE bytes at a time.
         * 
         * @param channel The channel to send the message on (CTRL, SEND, or RECV).
         * @param timeout Maximum time in seconds to wait for a response from 
         *                the server. If None, it will wait indefinitely.
         * @return std::string The complete received message.
         */
        std::string receiveMessage(Comms::Channel channel, int* timeout);
        /**
         * @brief Asynchronously listens for a message from the server (with 
         *        threads).
         * 
         * This method is implemented similarly to receiveMessage(), however, it
         * uses asynchronous socket receiving dispatched on a separate thread 
         * which will set the returned future string.
         * 
         * @param channel 
         * @param timeout 
         * @return std::future<std::string> 
         */
        std::future<std::string> listenForMessage(Comms::Channel channel, int* timeout);
        std::map<Channel, int> conns;
    private:
        std::string serverAddress;
        int serverPort;
    };

    /**
     * @brief Base class for database connectors.
     * 
     * Database connectors enable NSB to use a database to store and retrieve 
     * larger messages being sent over the network. To use this, the 
     * _database.use_db_ parameter must be configured to be __true__. This class 
     * itself does not contain the necessary _store()_, _checkOut()_, and 
     * _peek()_ methods necessary for NSB, but it can be inherited by other 
     * connector implications that implement those methods. This class does 
     * provide a basic message key generator.
     * 
     * @see RedisConnector
     */
    class DBConnector {
    public:
        /**
         * @brief Base class constructor for a new DBConnector object.
         * 
         * @param clientIdentifier The identifier for the owning client.
         */
        DBConnector(const std::string& clientIdentifier);
        /** @brief Base class deconstructor for a DBConnector object. */
        ~DBConnector();
    protected:
        std::string clientId;
        int plctr;
        std::mutex mtx;
        /**
         * @brief Generates a new ID for payloads to be used as a key.
         * 
         * Uses a object counter (plctr), the client ID, and the current time 
         * to generate payload IDs to be used as keys with database systems.
         * 
         * @return std::string The generated key.
         */
        std::string generatePayloadId() {
            std::lock_guard<std::mutex> lock(mtx);
            plctr = (plctr + 1) & 0xFFFFF;
            int tms = static_cast<int>(std::chrono::system_clock::now().time_since_epoch().count());
            std::string payloadId = std::to_string(tms) + "-" + clientId + "-" + std::to_string(plctr);
            return payloadId;
        }
    };

    /**
     * @brief Connector for offloading payloads to a Redis database.
     * 
     * A database connector that enables NSB to utilize Redis to store payloads 
     * in a shared memory store to avoid incurring the overhead of moving larger 
     * payloads over sockets. Using this option may be beneficial for 
     * applications with larger payloads (>32KB). Database can be configured in 
     * the configuration file.
     */
    class RedisConnector : public DBConnector {
    public:
        /**
         * @brief Constructor for a new RedisConnector.
         * 
         * This constructor connects to the Redis server instance which must be
         * started from outside this program.
         * 
         * @param clientIdentifier The identifier of the client using this 
         *                         connector.
         * @param db_address The address of the Redis server.
         * @param db_port The port to be used to access the Redis server.
         */
        RedisConnector(const std::string& clientIdentifier, std::string& db_address, int db_port);
        /**
         * @brief Destructor for the RedisConnector object.
         * 
         * Closes the Redis connection if it's still open.
         */
        ~RedisConnector();
        /**
         * @brief Checks connection to the Redis server.
         * 
         * This method checks if the Redis context is still active.
         * 
         * @return bool Whether or not the connection to the server is alive.
         */
        bool isConnected() const;
        /**
         * @brief Stores a payload.
         * 
         * This method creates a new unique payload key and then stores the 
         * payload under that key.
         * 
         * @param value The value (payload) to be stored.
         * @return std::string The generated key the message was stored with.
         */
        std::string store(const std::string& value);
        /**
         * @brief Checks out a payload.
         * 
         * This method uses the passed-in key to check out a payload, deleting 
         * the paylaod after it has been retrieved. This method is expected to 
         * be used in the final retrieval in the lifecycle of a payload.
         * 
         * @param key The key to retrieve the payload from the Redis server.
         * @return std::string NSBAppClient::receive()
         */
        std::string checkOut(std::string& key);
        /**
         * @brief Peeks at the payload at the given key.
         * 
         * This method uses the passed-in key to retrieve a payload, similar to 
         * the checkOut() method, but without deleting the stored value. This is
         * expected to be used when payloads are fetched before they must be 
         * accessible again later in the payload's lifetime.
         * 
         * @param key The key to retrieve the payload from the Redis server.
         * @return std::string The retrieved payload.
         */
        std::string peek(const int key);
    private:
        std::string address;
        int port;
        int num;
        redisContext* context;
        bool connect();
        void disconnect();
    };

    

    
}

#endif // NSB_H
