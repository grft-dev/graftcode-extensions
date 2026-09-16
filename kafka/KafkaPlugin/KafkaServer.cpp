#include "KafkaServer.h"
#include "KafkaUtil.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#if __has_include(<librdkafka/rdkafkacpp.h>)
#include <librdkafka/rdkafkacpp.h>
#elif __has_include(<rdkafkacpp.h>)
#include <rdkafkacpp.h>
#else
#error "librdkafka C++ header not found (expected librdkafka/rdkafkacpp.h or rdkafkacpp.h)"
#endif

namespace {

void logInfo(const std::string& msg) {
    std::cout << "[KafkaServer][INFO] " << msg << std::endl;
}

void logWarn(const std::string& msg) {
    std::cout << "[KafkaServer][WARN] " << msg << std::endl;
}

}  // namespace

KafkaServer::~KafkaServer() { stop(); }

void KafkaServer::configure(const char* jsonConfig, ProcessMessageFn processMessage) {
    {
        std::lock_guard<std::mutex> lock(processMutex_);
        process_ = processMessage;
    }
    if (!jsonConfig) {
        return;
    }
    auto j = nlohmann::json::parse(jsonConfig, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return;
    }
    if (j.contains("brokers") && j["brokers"].is_string()) {
        brokers_ = j["brokers"].get<std::string>();
    } else if (j.contains("host") && j["host"].is_string()) {
        brokers_ = j["host"].get<std::string>();  // compatibility with other plugins
    }
    if (j.contains("requestTopic") && j["requestTopic"].is_string()) {
        requestTopic_ = j["requestTopic"].get<std::string>();
    } else if (j.contains("queue") && j["queue"].is_string()) {
        requestTopic_ = j["queue"].get<std::string>();
    }
    if (j.contains("replyTopic") && j["replyTopic"].is_string()) {
        replyTopic_ = j["replyTopic"].get<std::string>();
    } else if (j.contains("replyQueue") && j["replyQueue"].is_string()) {
        replyTopic_ = j["replyQueue"].get<std::string>();
    }
    if (j.contains("groupId") && j["groupId"].is_string()) {
        groupId_ = j["groupId"].get<std::string>();
    }
    if (j.contains("securityProtocol") && j["securityProtocol"].is_string()) {
        securityProtocol_ = j["securityProtocol"].get<std::string>();
    }
    if (j.contains("saslMechanism") && j["saslMechanism"].is_string()) {
        saslMechanism_ = j["saslMechanism"].get<std::string>();
    }
    if (j.contains("saslUsername") && j["saslUsername"].is_string()) {
        saslUsername_ = j["saslUsername"].get<std::string>();
    }
    if (j.contains("saslPassword") && j["saslPassword"].is_string()) {
        saslPassword_ = j["saslPassword"].get<std::string>();
    }
    if (j.contains("sslCaLocation") && j["sslCaLocation"].is_string()) {
        sslCaLocation_ = j["sslCaLocation"].get<std::string>();
    }
}

void KafkaServer::applySecurity(RdKafka::Conf* conf) const {
    using KafkaPluginUtil::setConfOrThrow;
    if (!securityProtocol_.empty()) {
        setConfOrThrow(conf, "security.protocol", securityProtocol_);
    }
    if (!saslMechanism_.empty()) {
        setConfOrThrow(conf, "sasl.mechanism", saslMechanism_);
    }
    if (!saslUsername_.empty()) {
        setConfOrThrow(conf, "sasl.username", saslUsername_);
    }
    if (!saslPassword_.empty()) {
        setConfOrThrow(conf, "sasl.password", saslPassword_);
    }
    if (!sslCaLocation_.empty()) {
        setConfOrThrow(conf, "ssl.ca.location", sslCaLocation_);
    }
}

void KafkaServer::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread([this] { loop(); });
}

void KafkaServer::stop() {
    if (!running_.exchange(false)) {
        if (worker_.joinable()) {
            worker_.join();
        }
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void KafkaServer::publishReply(RdKafka::Producer* producer,
                               const std::string& replyTopic,
                               const std::string& correlationId,
                               const std::vector<unsigned char>& response) {
    RdKafka::Headers* headers = RdKafka::Headers::create();
    if (!correlationId.empty()) {
        headers->add("correlation-id", correlationId);
    }

    void* payload = nullptr;
    std::size_t len = 0;
    if (!response.empty()) {
        payload = const_cast<unsigned char*>(response.data());
        len = response.size();
    }

    const RdKafka::ErrorCode err = producer->produce(
        replyTopic,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        payload,
        len,
        nullptr,
        0,
        0,
        headers,
        nullptr);

    if (err != RdKafka::ERR_NO_ERROR) {
        delete headers;
        throw std::runtime_error("Failed to publish reply to '" + replyTopic +
                                 "': " + RdKafka::err2str(err));
    }
    producer->poll(0);
}

void KafkaServer::loop() {
    logInfo("starting consumer on topic='" + requestTopic_ + "' group='" + groupId_ +
            "' brokers='" + brokers_ + "'");

    while (running_) {
        std::unique_ptr<RdKafka::Producer> producer;
        std::unique_ptr<RdKafka::KafkaConsumer> consumer;

        try {
            using KafkaPluginUtil::setConfOrThrow;
            std::string errstr;

            {
                std::unique_ptr<RdKafka::Conf> pconf(
                    RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
                setConfOrThrow(pconf.get(), "bootstrap.servers", brokers_);
                setConfOrThrow(pconf.get(), "client.id", "graft-kafka-server-producer");
                applySecurity(pconf.get());
                RdKafka::Producer* raw = RdKafka::Producer::create(pconf.release(), errstr);
                if (!raw) {
                    throw std::runtime_error("Failed to create reply producer: " + errstr);
                }
                producer.reset(raw);
            }

            {
                std::unique_ptr<RdKafka::Conf> cconf(
                    RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
                setConfOrThrow(cconf.get(), "bootstrap.servers", brokers_);
                setConfOrThrow(cconf.get(), "group.id", groupId_);
                setConfOrThrow(cconf.get(), "client.id", "graft-kafka-server-consumer");
                setConfOrThrow(cconf.get(), "enable.auto.commit", "false");
                setConfOrThrow(cconf.get(), "auto.offset.reset", "earliest");
                setConfOrThrow(cconf.get(), "allow.auto.create.topics", "true");
                applySecurity(cconf.get());
                RdKafka::KafkaConsumer* raw =
                    RdKafka::KafkaConsumer::create(cconf.release(), errstr);
                if (!raw) {
                    throw std::runtime_error("Failed to create request consumer: " + errstr);
                }
                consumer.reset(raw);

                const RdKafka::ErrorCode subErr = consumer->subscribe({requestTopic_});
                if (subErr != RdKafka::ERR_NO_ERROR) {
                    throw std::runtime_error("Failed to subscribe to '" + requestTopic_ +
                                             "': " + RdKafka::err2str(subErr));
                }
            }

            logInfo("Kafka server loop connected");

            while (running_) {
                std::unique_ptr<RdKafka::Message> msg(consumer->consume(200));
                if (!msg) {
                    continue;
                }
                if (msg->err() == RdKafka::ERR__TIMED_OUT ||
                    msg->err() == RdKafka::ERR__PARTITION_EOF) {
                    producer->poll(0);
                    continue;
                }
                if (msg->err() != RdKafka::ERR_NO_ERROR) {
                    logWarn("consume error: " + msg->errstr());
                    continue;
                }

                ProcessMessageFn process = nullptr;
                {
                    std::lock_guard<std::mutex> lock(processMutex_);
                    process = process_;
                }
                if (!process) {
                    logWarn("processMessage callback is not configured; skipping message");
                    consumer->commitSync(msg.get());
                    continue;
                }

                const std::string correlationId =
                    KafkaPluginUtil::headerValue(msg->headers(), "correlation-id");
                std::string replyTo =
                    KafkaPluginUtil::headerValue(msg->headers(), "reply-to");
                if (replyTo.empty()) {
                    replyTo = replyTopic_;
                }

                std::vector<unsigned char> request;
                if (msg->payload() && msg->len() > 0) {
                    const auto* bytes = static_cast<const unsigned char*>(msg->payload());
                    request.assign(bytes, bytes + msg->len());
                }

                std::vector<unsigned char> response;
                auto writeResponse = [](void* context, const byte* data, std::size_t size) {
                    auto* out = static_cast<std::vector<unsigned char>*>(context);
                    if (!out) {
                        return;
                    }
                    if (!data || size == 0) {
                        out->clear();
                        return;
                    }
                    out->assign(data, data + size);
                };

                const bool ok =
                    process(request.data(), request.size(), writeResponse, &response);
                if (!ok) {
                    logWarn("processMessage returned false; not publishing reply");
                    // Still commit to avoid poison-pill loops; gateway owns retry policy.
                    consumer->commitSync(msg.get());
                    continue;
                }

                if (!replyTo.empty()) {
                    publishReply(producer.get(), replyTo, correlationId, response);
                    const RdKafka::ErrorCode flushErr = producer->flush(10000);
                    if (flushErr != RdKafka::ERR_NO_ERROR) {
                        logWarn("reply flush incomplete: " + RdKafka::err2str(flushErr));
                    }
                } else {
                    logWarn("no reply-to / replyTopic configured; dropping response");
                }

                consumer->commitSync(msg.get());
            }
        } catch (const std::exception& ex) {
            logWarn(std::string("loop error: ") + ex.what());
        } catch (...) {
            logWarn("loop error: unknown");
        }

        if (consumer) {
            try {
                consumer->close();
            } catch (...) {
            }
            consumer.reset();
        }
        if (producer) {
            try {
                producer->flush(2000);
            } catch (...) {
            }
            producer.reset();
        }

        if (running_) {
            logWarn("reconnecting in 2s...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    logInfo("stopped");
}
