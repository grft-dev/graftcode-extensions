#include "SqsServer.h"

#include "SqsUtil.h"

#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/DeleteMessageRequest.h>
#include <aws/sqs/model/MessageAttributeValue.h>
#include <aws/sqs/model/ReceiveMessageRequest.h>
#include <aws/sqs/model/SendMessageRequest.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Graftcode::Plugins::Sqs
{
    namespace
    {
        void LogInfo(const std::string& message)
        {
            std::cout << "[SqsServer][INFO] " << message << std::endl;
        }

        void LogWarn(const std::string& message)
        {
            std::cout << "[SqsServer][WARN] " << message << std::endl;
        }

        Aws::SQS::Model::MessageAttributeValue StringAttribute(
            const std::string& value)
        {
            Aws::SQS::Model::MessageAttributeValue attribute;
            attribute.SetDataType("String");
            attribute.SetStringValue(value.c_str());
            return attribute;
        }
    }

    SqsServer::~SqsServer()
    {
        stop();
    }

    void SqsServer::configure(
        const char* jsonConfig,
        ProcessMessageFn processMessage)
    {
        if (processMessage == nullptr) {
            throw std::runtime_error(
                "SQS server: processMessage callback is required");
        }

        SqsConfig config = ParseSqsConfigSource(
            jsonConfig != nullptr ? jsonConfig : "");
        ValidateServerConfig(config);

        std::lock_guard<std::mutex> lock(stateMutex_);
        if (running_.load(std::memory_order_acquire)) {
            throw std::runtime_error(
                "SQS server: cannot configure while running");
        }
        config_ = std::move(config);
        processMessage_ = processMessage;
    }

    void SqsServer::start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        stopRequested_.store(false, std::memory_order_release);
        SqsConfig config;
        ProcessMessageFn processMessage = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            config = config_;
            processMessage = processMessage_;
        }

        try {
            ValidateServerConfig(config);
            if (processMessage == nullptr) {
                throw std::runtime_error(
                    "SQS server: processMessage callback is not configured");
            }

            auto client = CreateAwsSqsClient(config);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                activeClient_ = client.get();
                if (stopRequested_.load(std::memory_order_acquire)) {
                    activeClient_->DisableRequestProcessing();
                }
            }
            LogInfo(
                "receiving from '" + config.requestQueueUrl +
                "' in region '" + config.region + "'");

            while (!stopRequested_.load(std::memory_order_acquire)) {
                Aws::SQS::Model::ReceiveMessageRequest receive;
                receive.SetQueueUrl(config.requestQueueUrl.c_str());
                receive.SetMaxNumberOfMessages(1);
                receive.SetWaitTimeSeconds(
                    static_cast<int>(config.waitTimeSeconds));
                receive.SetVisibilityTimeout(
                    static_cast<int>(config.visibilityTimeoutSeconds));
                receive.AddMessageAttributeNames("All");

                const auto receiveOutcome = client->ReceiveMessage(receive);
                if (!receiveOutcome.IsSuccess()) {
                    if (!stopRequested_.load(std::memory_order_acquire)) {
                        LogWarn(
                            "ReceiveMessage failed: " +
                            std::string(
                                receiveOutcome.GetError().GetMessage().c_str()));
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                    }
                    continue;
                }

                for (const auto& message :
                    receiveOutcome.GetResult().GetMessages()) {
                    if (stopRequested_.load(std::memory_order_acquire)) {
                        break;
                    }

                    try {
                        const std::string correlationId = GetStringAttribute(
                            message,
                            CorrelationIdAttribute);
                        const std::string replyQueueUrl =
                            config.oneWay
                                ? std::string()
                                : GetStringAttribute(
                                    message,
                                    ReplyToAttribute);
                        if (!config.oneWay &&
                            (correlationId.empty() ||
                                replyQueueUrl.empty())) {
                            LogWarn(
                                "RPC request is missing GraftcodeCorrelationId "
                                "or GraftcodeReplyTo; request left for "
                                "queue retry/DLQ policy");
                            continue;
                        }

                        const std::vector<unsigned char> request =
                            DecodePayload(message.GetBody());
                        std::vector<unsigned char> response;
                        const auto writeResponse = [](
                            void* context,
                            const byte* data,
                            std::size_t size) {
                            auto* output =
                                static_cast<std::vector<unsigned char>*>(context);
                            if (output == nullptr) {
                                return;
                            }
                            if (data == nullptr || size == 0) {
                                output->clear();
                                return;
                            }
                            output->assign(data, data + size);
                        };

                        if (!processMessage(
                                request.data(),
                                request.size(),
                                writeResponse,
                                &response)) {
                            LogWarn(
                                "processMessage returned false; request left "
                                "for queue retry/DLQ policy");
                            continue;
                        }

                        if (!replyQueueUrl.empty()) {
                            Aws::SQS::Model::SendMessageRequest send;
                            send.SetQueueUrl(replyQueueUrl.c_str());
                            send.SetMessageBody(EncodePayload(
                                response.data(),
                                response.size()));
                            send.AddMessageAttributes(
                                CorrelationIdAttribute,
                                StringAttribute(correlationId));
                            ConfigureFifoMessage(
                                send,
                                replyQueueUrl,
                                config.messageGroupId,
                                correlationId.empty()
                                    ? NewCorrelationId()
                                    : correlationId);

                            const auto sendOutcome = client->SendMessage(send);
                            if (!sendOutcome.IsSuccess()) {
                                LogWarn(
                                    "SendMessage reply failed: " +
                                    std::string(
                                        sendOutcome.GetError()
                                            .GetMessage().c_str()));
                                continue;
                            }
                        }

                        Aws::SQS::Model::DeleteMessageRequest remove;
                        remove.SetQueueUrl(config.requestQueueUrl.c_str());
                        remove.SetReceiptHandle(message.GetReceiptHandle());
                        const auto deleteOutcome =
                            client->DeleteMessage(remove);
                        if (!deleteOutcome.IsSuccess()) {
                            LogWarn(
                                "DeleteMessage failed: " +
                                std::string(
                                    deleteOutcome.GetError()
                                        .GetMessage().c_str()));
                        }
                    }
                    catch (const std::exception& error) {
                        LogWarn(
                            std::string("request processing failed: ") +
                            error.what());
                    }
                }
            }
            LogInfo("stopped");
        }
        catch (const std::exception& error) {
            LogWarn(std::string("server stopped after error: ") + error.what());
        }
        catch (...) {
            LogWarn("server stopped after unknown error");
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            activeClient_ = nullptr;
            running_.store(false, std::memory_order_release);
        }
        stoppedCondition_.notify_all();
    }

    void SqsServer::stop()
    {
        stopRequested_.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock(stateMutex_);
        if (activeClient_ != nullptr) {
            activeClient_->DisableRequestProcessing();
        }
        stoppedCondition_.wait(lock, [this]() {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

#if defined(_WIN32)
#define SQS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SQS_PLUGIN_EXPORT extern "C"
#endif

SQS_PLUGIN_EXPORT GraftcodeGateway::IServer* CreateServer()
{
    return new Graftcode::Plugins::Sqs::SqsServer();
}

SQS_PLUGIN_EXPORT void DestroyServer(GraftcodeGateway::IServer* server)
{
    delete server;
}
