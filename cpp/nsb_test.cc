#include "nsb.h"
#include "nsb_client.h"

int testSocketInterface() {
    using namespace nsb;
    // Testing
    LOG(INFO) << "Creating socket interface..." << std::endl;
    SocketInterface sif = SocketInterface(std::string("127.0.0.1"), 65432);
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
    return 0;
}

int testRedisConnector() {
    using namespace nsb;
    // Testing Redis Connector
    std::string thisAppId = "app1";
    std::string thatAppId = "app2";
    std::string redisServerAddr = "127.0.0.1";
    RedisConnector thisConn = RedisConnector(thisAppId, redisServerAddr, 5050);
    RedisConnector thatConn = RedisConnector(thatAppId, redisServerAddr, 5050);
    std::string sendPayload = "hola mundo";
    std::string key = thisConn.store(sendPayload);
    std::string recvPayload = thatConn.checkOut(key);
    DLOG(INFO) << "Payload sent: " << sendPayload << std::endl;
    DLOG(INFO) << "Payload received: " << recvPayload << std::endl;
    return 0;
}

int testLifecycle() {
    using namespace nsb;
    // Create app client.
    const std::string idApp1 = "node1";
    const std::string idApp2 = "node2";
    const std::string idSim1 = "node1";
    const std::string idSim2 = "node2";
    std::string nsbDaemonAddr = "127.0.0.1";
    int nsbDaemonPort = 65432;
    NSBAppClient app1 = NSBAppClient(idApp1, nsbDaemonAddr, nsbDaemonPort);
    NSBAppClient app2 = NSBAppClient(idApp2, nsbDaemonAddr, nsbDaemonPort);
    NSBSimClient sim1 = NSBSimClient(idSim1, nsbDaemonAddr, nsbDaemonPort);
    NSBSimClient sim2 = NSBSimClient(idSim1, nsbDaemonAddr, nsbDaemonPort);
    app1.ping();
    app2.ping();
    sim1.ping();
    sim2.ping();
    // Send a message.
    std::string payload1 = "Hello from app1";
    std::string payload2 = "Hola del app1";
    app1.send(idApp2, payload1);
    app1.send(idApp2, payload2);
    // Go through the simulator.
    for (int i=0; i<3; i++) {
        MessageEntry fetchedMsg = sim1.fetch(nullptr, 500);
        if (fetchedMsg.exists()) {
            sim2.post(idSim1, idApp2, fetchedMsg.payload_obj);
        } else {
            LOG(ERROR) << "No message to fetch." << std::endl;
        }
    }
    // Receive a message.
    for (int i=0; i<3; i++) {
        MessageEntry receivedMsg = app1.receive(nullptr, 500);
        if (receivedMsg.exists()) {
            LOG(INFO) << "Received payload: " << receivedMsg.payload_obj << std::endl;
        } else {
            LOG(ERROR) << "Didn't receive payload." << std::endl;
        }
    }
    // Receive a message.
    for (int i=0; i<3; i++) {
        MessageEntry receivedMsg = app2.receive(nullptr, 500);
        if (receivedMsg.exists()) {
            LOG(INFO) << "Received payload: " << receivedMsg.payload_obj << std::endl;
        } else {
            LOG(ERROR) << "Didn't receive payload." << std::endl;
        }
    }
    // Exit.
    app1.exit();
    return 0;
}

int main() {
    using namespace nsb;
    // Set up logging.
    NsbLogSink log_output = NsbLogSink();
    absl::InitializeLog();
    absl::log_internal::AddLogSink(&log_output);
    // return testSocketInterface();
    // return testRedisConnector();
    return testLifecycle();
}