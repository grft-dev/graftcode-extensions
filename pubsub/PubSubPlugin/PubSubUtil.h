#pragma once

#include "PubSubConfig.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace Graftcode::Plugins::PubSub
{
    inline constexpr const char* CorrelationIdAttribute = "correlationId";
    inline constexpr const char* ReplyToAttribute = "replyTo";
    // Decoded Pub/Sub message data limit.
    inline constexpr std::size_t MaxPayloadBytes = 10 * 1024 * 1024;

    class PubSubHttpError : public std::runtime_error
    {
    public:
        PubSubHttpError(long status, const std::string& message)
            : std::runtime_error(message)
            , status_(status)
        {
        }

        long status() const { return status_; }

    private:
        long status_;
    };

    struct PulledMessage
    {
        std::string ackId;
        std::vector<unsigned char> data;
        std::map<std::string, std::string> attributes;
    };

    std::string NewCorrelationId();
    std::string Base64Encode(const unsigned char* data, std::size_t size);
    std::vector<unsigned char> Base64Decode(const std::string& encoded);
    std::string BuildServiceAccountJwt(
        const std::string& clientEmail,
        const std::string& privateKeyPem,
        const std::string& tokenUri,
        std::int64_t issuedAtEpochSeconds);

    // One HTTP session against either the emulator or pubsub.googleapis.com.
    class PubSubSession
    {
    public:
        explicit PubSubSession(PubSubConfig config);
        ~PubSubSession();

        PubSubSession(const PubSubSession&) = delete;
        PubSubSession& operator=(const PubSubSession&) = delete;

        const PubSubConfig& Config() const { return config_; }

        void EnsureTopic(const std::string& topic);
        void EnsureSubscription(
            const std::string& subscription,
            const std::string& topic);
        void Publish(
            const std::string& topic,
            const unsigned char* data,
            std::size_t size,
            const std::map<std::string, std::string>& attributes);
        std::vector<PulledMessage> Pull(
            const std::string& subscription,
            std::uint32_t maxMessages);
        void Acknowledge(
            const std::string& subscription,
            const std::vector<std::string>& ackIds);

    private:
        struct HttpResult
        {
            long status{ 0 };
            bool timedOut{ false };
            std::string body;
            std::string error;
        };

        HttpResult Request(
            const char* method,
            const std::string& url,
            const std::string& body,
            const std::vector<std::string>& headers,
            long timeoutMs,
            bool authenticate);
        std::string ResourceUrl(
            const std::string& collection,
            const std::string& id) const;
        std::string BearerToken();
        void RefreshFromServiceAccount();
        bool TryMetadataToken();
        void ThrowIfHttpFailed(
            const char* operation,
            const HttpResult& result) const;

        PubSubConfig config_;
        std::string cachedToken_;
        std::int64_t tokenExpiryEpoch_{ 0 };
        void* curl_{ nullptr };
    };
}
