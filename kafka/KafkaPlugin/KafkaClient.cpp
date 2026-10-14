#include "KafkaClient.h"
#include "KafkaUtil.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#if __has_include(<librdkafka/rdkafkacpp.h>)
#include <librdkafka/rdkafkacpp.h>
#elif __has_include(<rdkafkacpp.h>)
#include <rdkafkacpp.h>
#else
#error "librdkafka C++ header not found (expected librdkafka/rdkafkacpp.h or rdkafkacpp.h)"
#endif

namespace {

void drainProducer(RdKafka::Producer* producer, int timeoutMs) {
    if (!producer) {
        return;
    }
    producer->flush(timeoutMs);
    while (producer->outq_len() > 0) {
        producer->poll(50);
    }
}

}  // namespace

class KafkaClient::DeliveryReports : public RdKafka::DeliveryReportCb {
public:
    explicit DeliveryReports(Graftcode::Plugins::Concurrency::PendingReplies& pending)
        : pending_(pending) {}

    void dr_cb(RdKafka::Message& message) override {
        std::unique_ptr<std::string> correlationId(static_cast<std::string*>(message.msg_opaque()));
        if (correlationId && message.err() != RdKafka::ERR_NO_ERROR) {
            pending_.Fail(*correlationId, "Kafka delivery failed for correlation-id=" +
                                              *correlationId + ": " + message.errstr());
        }
    }

private:
    Graftcode::Plugins::Concurrency::PendingReplies& pending_;
};

KafkaClient::KafkaClient(Config cfg) : cfg_(std::move(cfg)) {
    instanceId_ = KafkaPluginUtil::makeUuidV4();
    // Unique group so concurrent clients sharing one reply topic each get a full
    // copy of replies, then filter by correlation-id (broadcast-per-group model).
    consumerGroupId_ = cfg_.groupId.empty()
                           ? ("graft-client-" + instanceId_)
                           : (cfg_.groupId + "-" + instanceId_);
}

KafkaClient::~KafkaClient() {
    stopping_.store(true, std::memory_order_release);
    if (dispatcher_.joinable()) {
        dispatcher_.join();
    }
    pending_.Close("Kafka client is shutting down");
    if (consumer_) {
        consumer_->close();
        consumer_.reset();
    }
    if (producer_) {
        drainProducer(producer_.get(), 5000);
        producer_.reset();
    }
}

void KafkaClient::applySecurity(RdKafka::Conf* conf) const {
    using KafkaPluginUtil::setConfOrThrow;
    if (!cfg_.securityProtocol.empty()) {
        setConfOrThrow(conf, "security.protocol", cfg_.securityProtocol);
    }
    if (!cfg_.saslMechanism.empty()) {
        setConfOrThrow(conf, "sasl.mechanism", cfg_.saslMechanism);
    }
    if (!cfg_.saslUsername.empty()) {
        setConfOrThrow(conf, "sasl.username", cfg_.saslUsername);
    }
    if (!cfg_.saslPassword.empty()) {
        setConfOrThrow(conf, "sasl.password", cfg_.saslPassword);
    }
    if (!cfg_.sslCaLocation.empty()) {
        setConfOrThrow(conf, "ssl.ca.location", cfg_.sslCaLocation);
    }
}

void KafkaClient::ensureStarted() {
    if (started_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(startMutex_);
    if (started_.load(std::memory_order_acquire)) {
        return;
    }

    using KafkaPluginUtil::setConfOrThrow;
    std::string errstr;

    {
        std::unique_ptr<RdKafka::Conf> pconf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
        setConfOrThrow(pconf.get(), "bootstrap.servers", cfg_.brokers);
        setConfOrThrow(pconf.get(), "client.id", "graft-kafka-client-producer-" + instanceId_);
        setConfOrThrow(pconf.get(), "message.timeout.ms", std::to_string(cfg_.rpcTimeoutMs));
        applySecurity(pconf.get());
        if (!deliveryReports_) {
            deliveryReports_ = std::make_unique<DeliveryReports>(pending_);
        }
        if (pconf->set("dr_cb", deliveryReports_.get(), errstr) != RdKafka::Conf::CONF_OK) {
            throw std::runtime_error("Failed to set Kafka delivery report callback: " + errstr);
        }

        RdKafka::Producer* raw = RdKafka::Producer::create(pconf.release(), errstr);
        if (!raw) {
            throw std::runtime_error("Failed to create Kafka producer: " + errstr);
        }
        producer_.reset(raw);
    }

    {
        std::unique_ptr<RdKafka::Conf> cconf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
        setConfOrThrow(cconf.get(), "bootstrap.servers", cfg_.brokers);
        setConfOrThrow(cconf.get(), "group.id", consumerGroupId_);
        setConfOrThrow(cconf.get(), "client.id", "graft-kafka-client-consumer-" + instanceId_);
        setConfOrThrow(cconf.get(), "enable.auto.commit", "true");
        setConfOrThrow(cconf.get(), "auto.offset.reset", "latest");
        setConfOrThrow(cconf.get(), "allow.auto.create.topics", "true");
        applySecurity(cconf.get());

        RdKafka::KafkaConsumer* raw = RdKafka::KafkaConsumer::create(cconf.release(), errstr);
        if (!raw) {
            throw std::runtime_error("Failed to create Kafka consumer: " + errstr);
        }
        consumer_.reset(raw);

        const RdKafka::ErrorCode subErr = consumer_->subscribe({cfg_.replyTopic});
        if (subErr != RdKafka::ERR_NO_ERROR) {
            throw std::runtime_error("Failed to subscribe to reply topic '" + cfg_.replyTopic +
                                     "': " + RdKafka::err2str(subErr));
        }

        // Brief poll so the consumer joins the group / gets assignment before produce.
        // Without this, a fast reply can be published before the client is assigned.
        const auto warmDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (std::chrono::steady_clock::now() < warmDeadline) {
            std::unique_ptr<RdKafka::Message> msg(consumer_->consume(100));
            if (!msg) {
                continue;
            }
            if (msg->err() == RdKafka::ERR_NO_ERROR) {
                break;
            }
        }
    }

    dispatcher_ = std::thread(&KafkaClient::dispatchReplies, this);
    started_.store(true, std::memory_order_release);
}

void KafkaClient::dispatchReplies() {
    while (!stopping_.load(std::memory_order_acquire)) {
        producer_->poll(0);

        std::unique_ptr<RdKafka::Message> msg(consumer_->consume(100));
        if (!msg || msg->err() != RdKafka::ERR_NO_ERROR) {
            continue;
        }

        const std::string correlationId =
            KafkaPluginUtil::headerValue(msg->headers(), "correlation-id");
        if (correlationId.empty()) {
            continue;
        }

        std::vector<unsigned char> payload;
        if (msg->payload() != nullptr && msg->len() > 0) {
            const auto* bytes = static_cast<const unsigned char*>(msg->payload());
            payload.assign(bytes, bytes + msg->len());
        }
        pending_.Complete(correlationId, std::move(payload));
    }
}

std::vector<unsigned char> KafkaClient::call(const unsigned char* data, std::size_t len) {
    if (data == nullptr && len > 0) {
        throw std::invalid_argument("KafkaClient::call: null data with non-zero length");
    }

    ensureStarted();

    const std::string correlationId = KafkaPluginUtil::makeUuidV4();
    auto ticket = pending_.Register(correlationId);
    RdKafka::Headers* headers =
        KafkaPluginUtil::makeRpcHeaders(correlationId, cfg_.replyTopic);
    auto* deliveryTag = new std::string(correlationId);

    const RdKafka::ErrorCode produceErr = producer_->produce(
        cfg_.requestTopic,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        /*payload=*/const_cast<unsigned char*>(data ? data : reinterpret_cast<const unsigned char*>("")),
        /*len=*/len,
        /*key=*/nullptr,
        /*key_len=*/0,
        /*timestamp=*/0,
        headers,
        /*msg_opaque=*/deliveryTag);

    if (produceErr != RdKafka::ERR_NO_ERROR) {
        // produce() takes ownership of headers and the opaque tag only on success.
        delete headers;
        delete deliveryTag;
        throw std::runtime_error("Kafka produce to '" + cfg_.requestTopic +
                                 "' failed: " + RdKafka::err2str(produceErr));
    }

    std::vector<unsigned char> response;
    if (!ticket.Wait(std::chrono::milliseconds(cfg_.rpcTimeoutMs), response)) {
        throw std::runtime_error("Kafka RPC timed out after " + std::to_string(cfg_.rpcTimeoutMs) +
                                 " ms waiting for correlation-id=" + correlationId);
    }
    return response;
}
