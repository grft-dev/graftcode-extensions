#include "SqsClient.h"

#include "SqsUtil.h"

#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/DeleteMessageRequest.h>
#include <aws/sqs/model/MessageAttributeValue.h>
#include <aws/sqs/model/ReceiveMessageRequest.h>
#include <aws/sqs/model/SendMessageRequest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Graftcode::Plugins::Sqs
{
    namespace
    {
        Aws::SQS::Model::MessageAttributeValue StringAttribute(
            const std::string& value)
        {
            Aws::SQS::Model::MessageAttributeValue attribute;
            attribute.SetDataType("String");
            attribute.SetStringValue(value.c_str());
            return attribute;
        }

        std::runtime_error AwsError(
            const char* operation,
            const Aws::Client::AWSError<Aws::SQS::SQSErrors>& error)
        {
            return std::runtime_error(
                std::string("SQS ") + operation + " failed: " +
                error.GetExceptionName().c_str() + " - " +
                error.GetMessage().c_str());
        }
    }

    SqsClient::SqsClient(SqsConfig config)
        : config_(std::move(config))
    {
        ValidateClientConfig(config_);
        client_ = CreateAwsSqsClient(config_);
    }

    SqsClient::~SqsClient() = default;

    std::vector<unsigned char> SqsClient::Call(
        const unsigned char* data,
        std::size_t size)
    {
        if (size > 0 && data == nullptr) {
            throw std::invalid_argument(
                "SQS client: null payload with non-zero length");
        }

        // SQS cannot broker-filter a shared reply queue by correlation id.
        // Serializing calls keeps one configured reply queue deterministic for
        // this transport instance. Separate runtime instances should use
        // separate reply queues.
        std::lock_guard<std::mutex> lock(callMutex_);

        const std::string correlationId = NewCorrelationId();
        Aws::SQS::Model::SendMessageRequest send;
        send.SetQueueUrl(config_.requestQueueUrl.c_str());
        send.SetMessageBody(EncodePayload(data, size));
        send.AddMessageAttributes(
            CorrelationIdAttribute,
            StringAttribute(correlationId));
        if (!config_.oneWay) {
            send.AddMessageAttributes(
                ReplyToAttribute,
                StringAttribute(config_.replyQueueUrl));
        }
        ConfigureFifoMessage(
            send,
            config_.requestQueueUrl,
            config_.messageGroupId,
            correlationId);

        const auto sendOutcome = client_->SendMessage(send);
        if (!sendOutcome.IsSuccess()) {
            throw AwsError("SendMessage", sendOutcome.GetError());
        }

        if (config_.oneWay) {
            return {};
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(config_.rpcTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto remainingMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
            const auto remainingSeconds =
                static_cast<std::uint32_t>(std::max<std::int64_t>(
                    0,
                    remainingMs / 1000));

            Aws::SQS::Model::ReceiveMessageRequest receive;
            receive.SetQueueUrl(config_.replyQueueUrl.c_str());
            receive.SetMaxNumberOfMessages(10);
            receive.SetWaitTimeSeconds(static_cast<int>(
                (std::min)(config_.waitTimeSeconds, remainingSeconds)));
            receive.SetVisibilityTimeout(static_cast<int>(
                config_.visibilityTimeoutSeconds));
            receive.AddMessageAttributeNames("All");

            const auto receiveOutcome = client_->ReceiveMessage(receive);
            if (!receiveOutcome.IsSuccess()) {
                throw AwsError("ReceiveMessage", receiveOutcome.GetError());
            }

            for (const auto& message : receiveOutcome.GetResult().GetMessages()) {
                const auto deleteReply = [&]() {
                    Aws::SQS::Model::DeleteMessageRequest remove;
                    remove.SetQueueUrl(config_.replyQueueUrl.c_str());
                    remove.SetReceiptHandle(message.GetReceiptHandle());
                    const auto deleteOutcome = client_->DeleteMessage(remove);
                    if (!deleteOutcome.IsSuccess()) {
                        throw AwsError(
                            "DeleteMessage",
                            deleteOutcome.GetError());
                    }
                };

                if (GetStringAttribute(
                        message,
                        CorrelationIdAttribute) != correlationId) {
                    // Reply queues are required to be client-instance-dedicated.
                    // Remove stale responses so they cannot block a FIFO group.
                    deleteReply();
                    continue;
                }

                std::vector<unsigned char> response =
                    DecodePayload(message.GetBody());
                deleteReply();
                return response;
            }
        }

        throw std::runtime_error(
            "SQS RPC timed out after " +
            std::to_string(config_.rpcTimeoutMs) +
            " ms waiting for correlation id " + correlationId);
    }
}
