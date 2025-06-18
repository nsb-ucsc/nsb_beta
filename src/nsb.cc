// nsb.cc

#include "nsb.h"

namespace nsb {
    DBConnector::DBConnector(const std::string& clientIdentifier) : clientId(std::move(clientIdentifier)), plctr(0) {}
    
    DBConnector::~DBConnector() {}

    RedisConnector::RedisConnector(const std::string& clientIdentifier, std::string& db_address, int db_port) : 
        DBConnector(clientIdentifier), address(std::move(db_address)), port(db_port) {
        // Connect to Redis.
        if (connect()) {
            LOG(INFO) << "RedisConnector initialized!" << std::endl;
        }
    }

    RedisConnector::~RedisConnector() {
        // Close the connection if it is open.
        if (is_connected()) {
            disconnect();
        }
        LOG(INFO) << "RedisConnected shut down." << std::endl;
    }
    bool RedisConnector::is_connected() const {
        // Check if the connection is open.
        return context != nullptr && context->err == REDIS_OK;
    }
    bool RedisConnector::connect() {
        // Connect to the database and check for errors.
        context = redisConnect(address.c_str(), port);
        if (context->err) {
            LOG(ERROR) << context->errstr << std::endl;
            return false;
        }
        return true;
    }
    void RedisConnector::disconnect() {
        LOG(INFO) << "RedisConnector is gracefully disconnecting." << std::endl;
        redisFree(context);
    }

    std::string RedisConnector::store(const std::string& value) {
        DLOG(INFO) << "Storing payload: " << value << std::endl;
        std::string key = generatePayloadId();
        redisReply* reply = (redisReply*)redisCommand(context, "SET %s %s", key.c_str(), value.c_str());
        if (context->err) {
            LOG(ERROR) << "(SET Error) " << context->errstr << std::endl;
            return "";
        }
        DLOG(INFO) << "Payload stored. Reply: " << reply->str << std::endl;
        return key;
    }

    std::string RedisConnector::checkOut(std::string& key) {
        DLOG(INFO) << "Retrieving payload with key:" << key << std::endl;
        redisReply* reply = (redisReply*)redisCommand(context, "GET %s", key.c_str());
        return reply->str;
    }
}