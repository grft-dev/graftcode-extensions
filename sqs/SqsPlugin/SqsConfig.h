#pragma once

#include <cstdint>
#include <string>

namespace Graftcode::Plugins::Sqs
{
    struct SqsConfig
    {
        std::string name;
        std::string region{ "us-east-1" };
        std::string endpointOverride;
        std::string accessKeyId;
        std::string secretAccessKey;
        std::string sessionToken;
        std::string requestQueueUrl;
        std::string replyQueueUrl;
        std::string messageGroupId{ "graftcode" };
        std::uint32_t rpcTimeoutMs{ 30000 };
        std::uint32_t waitTimeSeconds{ 1 };
        std::uint32_t visibilityTimeoutSeconds{ 30 };
        bool oneWay{ false };
        bool verifySsl{ true };
    };

    SqsConfig ParseSqsConfigSource(const std::string& configSource);
    void ValidateClientConfig(const SqsConfig& config);
    void ValidateServerConfig(const SqsConfig& config);
    bool IsFifoQueueUrl(const std::string& queueUrl);
}
