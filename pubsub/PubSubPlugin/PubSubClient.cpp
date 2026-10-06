#include "PubSubClient.h"

#include "PubSubUtil.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Graftcode::Plugins::PubSub
{
    namespace
    {
        void SleepBriefly()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    PubSubClient::PubSubClient(PubSubConfig config)
        : config_(std::move(config))
    {
        ApplyRuntimeEnvironment(config_);
        ValidateClientConfig(config_);
    }

    PubSubClient::~PubSubClient() = default;

    void PubSubClient::EnsureResources()
    {
        if (session_ == nullptr) {
            session_ = std::make_unique<PubSubSession>(config_);
        }
        if (resourcesReady_ || !config_.autoCreate) {
            return;
        }

        session_->EnsureTopic(config_.requestTopic);
        if (!config_.oneWay) {
            session_->EnsureTopic(config_.replyTopic);
            session_->EnsureSubscription(
                config_.replySubscription,
                config_.replyTopic);
        }
        resourcesReady_ = true;
    }

    std::vector<unsigned char> PubSubClient::Call(
        const unsigned char* data,
        std::size_t size)
    {
        if (size > MaxPayloadBytes) {
            throw std::runtime_error(
                "Pub/Sub plugin: payload exceeds the 10 MiB Pub/Sub limit");
        }
        if (size > 0 && data == nullptr) {
            throw std::invalid_argument(
                "Pub/Sub client: null payload with non-zero length");
        }

        // A reply subscription is dedicated to this transport. Calls are
        // serialized so an unrelated reply can be acked instead of being
        // delivered to a second in-flight call.
        std::lock_guard<std::mutex> lock(callMutex_);
        EnsureResources();

        const std::string correlationId = NewCorrelationId();
        std::map<std::string, std::string> attributes;
        attributes.emplace(CorrelationIdAttribute, correlationId);
        if (!config_.oneWay) {
            attributes.emplace(ReplyToAttribute, config_.replyTopic);
        }

        const auto publishDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            try {
                session_->Publish(
                    config_.requestTopic,
                    data,
                    size,
                    attributes);
                break;
            }
            catch (const PubSubHttpError& error) {
                const bool retry = error.status() == 404 &&
                    std::chrono::steady_clock::now() < publishDeadline;
                if (!retry) {
                    throw;
                }
                SleepBriefly();
            }
        }

        if (config_.oneWay) {
            return {};
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(config_.rpcTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            std::vector<PulledMessage> batch;
            try {
                batch = session_->Pull(
                    config_.replySubscription,
                    config_.maxMessages);
            }
            catch (const PubSubHttpError& error) {
                if (error.status() == 429 || error.status() >= 500 ||
                    error.status() == 0) {
                    SleepBriefly();
                    continue;
                }
                throw;
            }

            std::vector<std::string> ackIds;
            std::vector<unsigned char> response;
            bool found = false;
            for (auto& message : batch) {
                if (message.ackId.empty()) {
                    continue;
                }
                ackIds.push_back(message.ackId);
                const auto correlation = message.attributes.find(
                    CorrelationIdAttribute);
                if (!found &&
                    correlation != message.attributes.end() &&
                    correlation->second == correlationId) {
                    response = std::move(message.data);
                    found = true;
                }
            }

            if (!ackIds.empty()) {
                try {
                    session_->Acknowledge(config_.replySubscription, ackIds);
                }
                catch (const std::exception&) {
                    if (!found) {
                        throw;
                    }
                }
            }
            if (found) {
                return response;
            }
            SleepBriefly();
        }

        throw std::runtime_error(
            "Pub/Sub RPC timed out after " +
            std::to_string(config_.rpcTimeoutMs) +
            " ms waiting for correlation id " + correlationId);
    }
}
