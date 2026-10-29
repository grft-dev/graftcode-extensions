#include "PubSubClient.h"

#include "PubSubUtil.h"

#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
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

    PubSubClient::~PubSubClient()
    {
        stopping_.store(true, std::memory_order_release);
        pending_.Close("Pub/Sub client is shutting down");
        if (dispatcher_.joinable()) {
            dispatcher_.join();
        }
    }

    std::unique_ptr<PubSubSession> PubSubClient::AcquireSession()
    {
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            if (!idleSessions_.empty()) {
                auto session = std::move(idleSessions_.back());
                idleSessions_.pop_back();
                return session;
            }
        }
        return std::make_unique<PubSubSession>(config_);
    }

    void PubSubClient::ReleaseSession(std::unique_ptr<PubSubSession> session)
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        idleSessions_.push_back(std::move(session));
    }

    void PubSubClient::EnsureStarted()
    {
        if (started_.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(startMutex_);
        if (started_.load(std::memory_order_acquire)) {
            return;
        }

        auto session = AcquireSession();
        if (config_.autoCreate) {
            session->EnsureTopic(config_.requestTopic);
            if (!config_.oneWay) {
                session->EnsureTopic(config_.replyTopic);
                session->EnsureSubscription(
                    config_.replySubscription,
                    config_.replyTopic);
            }
        }
        ReleaseSession(std::move(session));

        if (!config_.oneWay) {
            replySession_ = std::make_unique<PubSubSession>(config_);
            dispatcher_ = std::thread(&PubSubClient::DispatchReplies, this);
        }
        started_.store(true, std::memory_order_release);
    }

    void PubSubClient::DispatchReplies()
    {
        while (!stopping_.load(std::memory_order_acquire)) {
            if (!pending_.WaitForPending(std::chrono::milliseconds(200))) {
                continue;
            }

            std::vector<PulledMessage> batch;
            try {
                batch = replySession_->Pull(
                    config_.replySubscription,
                    config_.maxMessages);
            }
            catch (const PubSubHttpError& error) {
                if (error.status() != 429 && error.status() < 500 &&
                    error.status() != 0) {
                    pending_.FailAll(error.what());
                }
                SleepBriefly();
                continue;
            }
            catch (const std::exception& error) {
                pending_.FailAll(error.what());
                SleepBriefly();
                continue;
            }

            if (batch.empty()) {
                SleepBriefly();
                continue;
            }

            // Replies nobody waits for are acknowledged too, so they are not redelivered.
            std::vector<std::string> ackIds;
            for (const auto& message : batch) {
                if (!message.ackId.empty()) {
                    ackIds.push_back(message.ackId);
                }
            }
            if (!ackIds.empty()) {
                try {
                    replySession_->Acknowledge(config_.replySubscription, ackIds);
                }
                catch (const std::exception&) {
                }
            }

            for (auto& message : batch) {
                const auto correlation = message.attributes.find(
                    CorrelationIdAttribute);
                if (correlation != message.attributes.end()) {
                    pending_.Complete(correlation->second, std::move(message.data));
                }
            }
        }
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

        EnsureStarted();

        const std::string correlationId = NewCorrelationId();
        std::optional<Concurrency::PendingReplies::Ticket> ticket;
        std::map<std::string, std::string> attributes;
        attributes.emplace(CorrelationIdAttribute, correlationId);
        if (!config_.oneWay) {
            attributes.emplace(ReplyToAttribute, config_.replyTopic);
            ticket.emplace(pending_.Register(correlationId));
        }

        auto session = AcquireSession();
        const auto publishDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            try {
                session->Publish(
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
        ReleaseSession(std::move(session));

        if (config_.oneWay) {
            return {};
        }

        std::vector<unsigned char> response;
        if (!ticket->Wait(std::chrono::milliseconds(config_.rpcTimeoutMs), response)) {
            throw std::runtime_error(
                "Pub/Sub RPC timed out after " +
                std::to_string(config_.rpcTimeoutMs) +
                " ms waiting for correlation id " + correlationId);
        }
        return response;
    }
}
