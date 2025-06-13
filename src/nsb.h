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

namespace nsb {

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

        Config() : SYSTEM_MODE(SystemMode::PULL), USE_DB(false), DB_ADDRESS(""), DB_PORT(0), DB_NUM(0) {}
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
        DBConnector(std::string& clientIdentifier);
        ~DBConnector();
    protected:
        std::string clientId;
        int plctr;
        std::mutex mtx;
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
        RedisConnector(std::string& clientIdentifier, const std::string& db_address, int db_port);
        ~RedisConnector();
        bool is_connected() const;
        std::string store(const std::string& value);
        std::string checkOut(std::string& key);
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
