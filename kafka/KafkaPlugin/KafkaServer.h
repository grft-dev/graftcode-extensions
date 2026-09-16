#pragma once

#include "IServer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace RdKafka {
class Producer;
class KafkaConsumer;
class Conf;
}  // namespace RdKafka

class KafkaServer : public GraftcodeGateway::IServer {
public:
    KafkaServer() = default;
    ~KafkaServer() override;

    void configure(const char* jsonConfig, ProcessMessageFn processMessage) override;
    void start() override;
    void stop() override;

private:
    void loop();
    void applySecurity(RdKafka::Conf* conf) const;
    void publishReply(RdKafka::Producer* producer,
                      const std::string& replyTopic,
                      const std::string& correlationId,
                      const std::vector<unsigned char>& response);

    ProcessMessageFn process_{nullptr};
    std::mutex processMutex_;

    std::string brokers_ = "localhost:9092";
    std::string requestTopic_ = "graft.requests";
    std::string replyTopic_ = "graft.replies";
    std::string groupId_ = "graft-gateway";
    std::string securityProtocol_;
    std::string saslMechanism_;
    std::string saslUsername_;
    std::string saslPassword_;
    std::string sslCaLocation_;

    std::atomic<bool> running_{false};
    std::thread worker_;
};
