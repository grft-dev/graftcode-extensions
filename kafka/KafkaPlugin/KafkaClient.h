#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace RdKafka {
class Conf;
class Producer;
class KafkaConsumer;
}  // namespace RdKafka

// Client-side RPC over Kafka: produce to request topic, consume correlated reply.
class KafkaClient {
public:
    struct Config {
        std::string brokers = "localhost:9092";
        std::string requestTopic = "graft.requests";
        std::string replyTopic = "graft.replies";
        std::string groupId = "graft-client";
        int rpcTimeoutMs = 30000;
        // Optional SASL/SSL pass-through (empty = leave librdkafka defaults).
        std::string securityProtocol;   // e.g. "SASL_SSL", "SSL", "PLAINTEXT"
        std::string saslMechanism;      // e.g. "PLAIN", "SCRAM-SHA-512"
        std::string saslUsername;
        std::string saslPassword;
        std::string sslCaLocation;
    };

    explicit KafkaClient(Config cfg);
    ~KafkaClient();

    KafkaClient(const KafkaClient&) = delete;
    KafkaClient& operator=(const KafkaClient&) = delete;

    // Returns response bytes; throws on timeout or produce/consume failure.
    std::vector<unsigned char> call(const unsigned char* data, std::size_t len);

private:
    void applySecurity(RdKafka::Conf* conf) const;
    void ensureStarted();

    Config cfg_;
    std::string instanceId_;
    std::string consumerGroupId_;
    std::unique_ptr<RdKafka::Producer> producer_;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
    std::mutex callMutex_;
    bool started_ = false;
};
