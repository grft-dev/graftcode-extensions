#pragma once

#include "SqsConfig.h"

#include <aws/sqs/model/Message.h>
#include <aws/sqs/model/SendMessageRequest.h>

#include <memory>
#include <string>
#include <vector>

namespace Aws::SQS
{
    class SQSClient;
}

namespace Graftcode::Plugins::Sqs
{
    inline constexpr const char* CorrelationIdAttribute =
        "GraftcodeCorrelationId";
    inline constexpr const char* ReplyToAttribute = "GraftcodeReplyTo";

    std::unique_ptr<Aws::SQS::SQSClient> CreateAwsSqsClient(
        const SqsConfig& config);
    Aws::String EncodePayload(const unsigned char* data, std::size_t size);
    std::vector<unsigned char> DecodePayload(const Aws::String& body);
    std::string NewCorrelationId();
    std::string GetStringAttribute(
        const Aws::SQS::Model::Message& message,
        const char* name);
    void ConfigureFifoMessage(
        Aws::SQS::Model::SendMessageRequest& request,
        const std::string& queueUrl,
        const std::string& groupId,
        const std::string& deduplicationId);
}
