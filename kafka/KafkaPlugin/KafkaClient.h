#pragma once

#include "PendingReplies.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace RdKafka {
class Conf;
class Producer;
class KafkaConsumer;
}  // namespace RdKafka

// Client-side RPC over Kafka: produce to request topic, consume correlated reply.
// call() may run concurrently; one consumer thread dispatches replies by correlation id.
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
    class DeliveryReports;

    void applySecurity(RdKafka::Conf* conf) const;
    void ensureStarted();
    void dispatchReplies();

    Config cfg_;
    std::string instanceId_;
    std::string consumerGroupId_;
    Graftcode::Plugins::Concurrency::PendingReplies pending_;
    std::unique_ptr<DeliveryReports> deliveryReports_;
    std::unique_ptr<RdKafka::Producer> producer_;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
    std::thread dispatcher_;
    std::mutex startMutex_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stopping_{false};
};
