// nsb.cc

#include "nsb.h"

namespace nsb {
    DBConnector::DBConnector(std::string& clientIdentifier) : clientId(std::move(clientIdentifier)), plctr(0) {}
    
    DBConnector::~DBConnector() {}

    RedisConnector::RedisConnector(std::string& clientIdentifier, const std::string& db_address, int db_port) : 
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
        context = redisAsyncConnect(address.c_str(), port);
        if (context->err) {
            LOG(ERROR) << context->errstr << std::endl;
            return false;
        }
        // Set callbacks for connection response.
        redisAsyncSetConnectCallback(context, connectCallback);
        redisAsyncSetDisconnectCallback(context, disconnectCallback);
        return true;
    }
    void RedisConnector::disconnect() {
        LOG(INFO) << "RedisConnector is gracefully disconnecting." << std::endl;
        redisAsyncDisconnect(context);
    }
    std::string RedisConnector::store(const std::string& value) {
        DLOG(INFO) << "Storing payload: " << value << std::endl;
        std::string key = generatePayloadId();
        redisAsyncCommand(context, setCallback, &key, "SET %d %s", key, value);
        if (context->err) {
            LOG(ERROR) << "(SET Error) " << context->errstr << std::endl;
            return "";
        }
        DLOG(INFO) << "Payload stored with key: " << key << std::endl;
        return key;
    }

    std::string RedisConnector::checkOut(std::string& key) {
        DLOG(INFO) << "Retrieving payload with key:" << key << std::endl;
        std::string value;
        redisAsyncCommand(context, getCallback, &value, "GET %s", key.c_str());
    }

    /* CALLBACK FUNCTIONS */

    void RedisConnector::connectCallback(const redisAsyncContext* c, int status) {
        if (status != REDIS_OK) {
            LOG(ERROR) << "(Connection Error) " << c->errstr << std::endl;
            return;
        }
        LOG(INFO) << "Connected to database." << std::endl;
    }

    void RedisConnector::disconnectCallback(const redisAsyncContext* c, int status) {
        if (status != REDIS_OK) {
            LOG(ERROR) << "(Disconnection Error) " << c->errstr << std::endl;
        }
        LOG(INFO) << "Disconnected from database." << std::endl;
    }

    void RedisConnector::setCallback(redisAsyncContext* c, void* r, void* privdata) {
        if (c->err) {
            LOG(ERROR) << "(SET Error) " << c->errstr << std::endl;
            return;
        }
        int* key = static_cast<int*>(privdata);
        redisReply* reply = static_cast<redisReply*>(r);
        DLOG(INFO) << "Value set with key " << key << ". Reply: " << reply->str << std::endl;
    }

    void RedisConnector::getCallback(redisAsyncContext* c, void* r, void* privdata) {
        if (c->err) {
            LOG(ERROR) << "(GET Error) " << c->errstr << std::endl;
            return;
        }
        int* key = static_cast<int*>(privdata);
        redisReply* reply = static_cast<redisReply*>(r);
        DLOG(INFO) << "Got value with key " << key << ". Reply: " << reply->str << std::endl;
    }

    
}