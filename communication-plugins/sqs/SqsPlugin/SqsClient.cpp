#include "SqsClient.h"

#include "SqsUtil.h"

#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/DeleteMessageRequest.h>
#include <aws/sqs/model/MessageAttributeValue.h>
#include <aws/sqs/model/ReceiveMessageRequest.h>
#include <aws/sqs/model/SendMessageRequest.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
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

    SqsClient::~SqsClient()
    {
        stopping_.store(true, std::memory_order_release);
        pending_.Close("SQS client is shutting down");
        if (dispatcher_.joinable()) {
            dispatcher_.join();
        }
    }

    void SqsClient::EnsureDispatcher()
    {
        if (started_.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(startMutex_);
        if (started_.load(std::memory_order_acquire)) {
            return;
        }
        dispatcher_ = std::thread(&SqsClient::DispatchReplies, this);
        started_.store(true, std::memory_order_release);
    }

    void SqsClient::DispatchReplies()
    {
        while (!stopping_.load(std::memory_order_acquire)) {
            if (!pending_.WaitForPending(std::chrono::milliseconds(200))) {
                continue;
            }

            Aws::SQS::Model::ReceiveMessageRequest receive;
            receive.SetQueueUrl(config_.replyQueueUrl.c_str());
            receive.SetMaxNumberOfMessages(10);
            receive.SetWaitTimeSeconds(static_cast<int>(config_.waitTimeSeconds));
            receive.SetVisibilityTimeout(static_cast<int>(
                config_.visibilityTimeoutSeconds));
            receive.AddMessageAttributeNames("All");

            const auto receiveOutcome = client_->ReceiveMessage(receive);
            if (!receiveOutcome.IsSuccess()) {
                pending_.FailAll(
                    AwsError("ReceiveMessage", receiveOutcome.GetError()).what());
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            for (const auto& message : receiveOutcome.GetResult().GetMessages()) {
                const std::string correlationId =
                    GetStringAttribute(message, CorrelationIdAttribute);

                // Reply queues are client-instance-dedicated, so replies nobody
                // waits for are stale and are removed to keep FIFO groups moving.
                Aws::SQS::Model::DeleteMessageRequest remove;
                remove.SetQueueUrl(config_.replyQueueUrl.c_str());
                remove.SetReceiptHandle(message.GetReceiptHandle());
                const auto deleteOutcome = client_->DeleteMessage(remove);
                if (!deleteOutcome.IsSuccess()) {
                    pending_.Fail(
                        correlationId,
                        AwsError("DeleteMessage", deleteOutcome.GetError()).what());
                    continue;
                }

                try {
                    pending_.Complete(correlationId, DecodePayload(message.GetBody()));
                }
                catch (const std::exception& error) {
                    pending_.Fail(correlationId, error.what());
                }
            }
        }
    }

    std::vector<unsigned char> SqsClient::Call(
        const unsigned char* data,
        std::size_t size)
    {
        if (size > 0 && data == nullptr) {
            throw std::invalid_argument(
                "SQS client: null payload with non-zero length");
        }

        const std::string correlationId = NewCorrelationId();
        std::optional<Concurrency::PendingReplies::Ticket> ticket;
        if (!config_.oneWay) {
            EnsureDispatcher();
            ticket.emplace(pending_.Register(correlationId));
        }

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

        std::vector<unsigned char> response;
        if (!ticket->Wait(std::chrono::milliseconds(config_.rpcTimeoutMs), response)) {
            throw std::runtime_error(
                "SQS RPC timed out after " +
                std::to_string(config_.rpcTimeoutMs) +
                " ms waiting for correlation id " + correlationId);
        }
        return response;
    }
}
