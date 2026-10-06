#pragma once

#include <cstdint>
#include <string>

namespace Graftcode::Plugins::PubSub
{
    struct PubSubConfig
    {
        std::string name;
        std::string projectId;
        std::string requestTopic;
        std::string requestSubscription;
        std::string replyTopic;
        std::string replySubscription;
        std::string emulatorHost;
        std::string credentialsFile;
        std::string accessToken;
        std::uint32_t rpcTimeoutMs{ 30000 };
        std::uint32_t ackDeadlineSeconds{ 60 };
        std::uint32_t maxMessages{ 10 };
        bool oneWay{ false };
        bool autoCreate{ false };
        bool autoCreateSpecified{ false };
        bool verifySsl{ true };
        bool emulatorUseHttps{ false };
    };

    PubSubConfig ParsePubSubConfigSource(const std::string& configSource);
    void ApplyRuntimeEnvironment(PubSubConfig& config);
    void ValidateClientConfig(const PubSubConfig& config);
    void ValidateServerConfig(const PubSubConfig& config);
    bool IsValidProjectId(const std::string& value);
    bool IsValidResourceId(const std::string& value);
    std::string EndpointBase(const PubSubConfig& config);
}
