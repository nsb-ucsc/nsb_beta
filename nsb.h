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
// Database.
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

namespace nsb {
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
        int DB_NUM;
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

    /**
     * @brief Connector for offloading payloads to a Redis database.
     * 
     * A database connector that enables NSB to utilize Redis to store payloads 
     * in a shared memory store to avoid incurring the overhead of moving larger 
     * payloads over sockets. Using this option may be beneficial for 
     * applications with larger payloads (>32KB). Database can be configured in 
     * the configuration file.
     */
    class RedisConnector {
    public:
        RedisConnector(const std::string& address, int port);
        ~RedisConnector();
        bool is_connected() const;
        bool store(const std::string& value);
        std::string check_out(const int key);
        std::string peek(const int key);
    private:
        std::string address;
        int port;
        int num;
        redisAsyncContext* context;
        int num_payloads;
        bool connect();
        void disconnect();
        // Callback Functions
        static void connectCallback(const redisAsyncContext* c, int status);
        static void disconnectCallback(const redisAsyncContext* c, int status);
        static void setCallback(const redisAsyncContext* c, void* r, void* privdata);
        static void getCallback(const redisAsyncContext* c, void* r, void* privdata);
    };
}

#endif // NSB_H
