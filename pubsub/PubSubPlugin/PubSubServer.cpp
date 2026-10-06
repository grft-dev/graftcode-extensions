#include "PubSubServer.h"

#include "PubSubUtil.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Graftcode::Plugins::PubSub
{
    namespace
    {
        void LogInfo(const std::string& message)
        {
            std::cout << "[PubSubServer][INFO] " << message << std::endl;
        }

        void LogWarn(const std::string& message)
        {
            std::cout << "[PubSubServer][WARN] " << message << std::endl;
        }

        void SleepUnlessStopped(
            const std::atomic_bool& stopRequested,
            std::chrono::milliseconds duration)
        {
            const auto deadline = std::chrono::steady_clock::now() + duration;
            while (!stopRequested.load(std::memory_order_acquire) &&
                std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        std::string Attribute(
            const std::map<std::string, std::string>& attributes,
            const char* name)
        {
            const auto value = attributes.find(name);
            if (value == attributes.end()) {
                return {};
            }
            return value->second;
        }

        void HandleMessage(
            PubSubSession& session,
            const PubSubConfig& config,
            GraftcodeGateway::IServer::ProcessMessageFn processMessage,
            const PulledMessage& message)
        {
            const std::string correlationId = Attribute(
                message.attributes,
                CorrelationIdAttribute);
            const std::string replyTo = config.oneWay
                ? std::string()
                : Attribute(message.attributes, ReplyToAttribute);

            if (!config.oneWay &&
                (correlationId.empty() ||
                    !IsValidResourceId(replyTo))) {
                LogWarn(
                    "RPC request is missing correlationId or a valid replyTo "
                    "topic; message left unacked for redelivery");
                return;
            }

            std::vector<unsigned char> response;
            const auto writeResponse = [](
                void* context,
                const unsigned char* data,
                std::size_t size) {
                auto* output = static_cast<std::vector<unsigned char>*>(context);
                if (output == nullptr) {
                    return;
                }
                if (data == nullptr || size == 0) {
                    output->clear();
                    return;
                }
                output->assign(data, data + size);
            };

            bool processed = false;
            try {
                processed = processMessage(
                    message.data.empty() ? nullptr : message.data.data(),
                    message.data.size(),
                    writeResponse,
                    &response);
            }
            catch (const std::exception& error) {
                LogWarn(std::string("processMessage threw: ") + error.what());
                return;
            }
            if (!processed) {
                LogWarn(
                    "processMessage returned false; message left unacked for "
                    "redelivery");
                return;
            }

            if (!replyTo.empty()) {
                try {
                    session.Publish(
                        replyTo,
                        response.data(),
                        response.size(),
                        {{CorrelationIdAttribute, correlationId}});
                }
                catch (const std::exception& error) {
                    LogWarn(std::string("reply publish failed: ") + error.what());
                    return;
                }
            }

            if (message.ackId.empty()) {
                LogWarn("request has no ackId; cannot acknowledge");
                return;
            }
            try {
                session.Acknowledge(
                    config.requestSubscription,
                    {message.ackId});
            }
            catch (const std::exception& error) {
                LogWarn(std::string("acknowledge failed: ") + error.what());
            }
        }
    }

    PubSubServer::~PubSubServer()
    {
        stop();
    }

    void PubSubServer::configure(
        const char* jsonConfig,
        ProcessMessageFn processMessage)
    {
        if (processMessage == nullptr) {
            throw std::runtime_error(
                "Pub/Sub server: processMessage callback is required");
        }

        PubSubConfig config = ParsePubSubConfigSource(
            jsonConfig != nullptr ? jsonConfig : "");
        ApplyRuntimeEnvironment(config);
        ValidateServerConfig(config);

        std::lock_guard<std::mutex> lock(stateMutex_);
        if (running_.load(std::memory_order_acquire)) {
            throw std::runtime_error(
                "Pub/Sub server: cannot configure while running");
        }
        config_ = std::move(config);
        processMessage_ = processMessage;
    }

    void PubSubServer::start()
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (running_.load(std::memory_order_relaxed)) {
                return;
            }
            stopRequested_.store(false, std::memory_order_relaxed);
            running_.store(true, std::memory_order_release);
        }

        PubSubConfig config;
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
                    "Pub/Sub server: processMessage callback is not configured");
            }

            PubSubSession session(config);
            if (config.autoCreate) {
                session.EnsureTopic(config.requestTopic);
                session.EnsureSubscription(
                    config.requestSubscription,
                    config.requestTopic);
            }

            LogInfo(
                "receiving subscription '" + config.requestSubscription +
                "' on topic '" + config.requestTopic +
                "' project '" + config.projectId +
                "' endpoint '" + EndpointBase(config) + "'");

            while (!stopRequested_.load(std::memory_order_acquire)) {
                try {
                    const auto batch = session.Pull(config.requestSubscription, 1);
                    if (batch.empty()) {
                        SleepUnlessStopped(
                            stopRequested_,
                            std::chrono::milliseconds(100));
                        continue;
                    }
                    for (const auto& message : batch) {
                        if (stopRequested_.load(std::memory_order_acquire)) {
                            break;
                        }
                        HandleMessage(session, config, processMessage, message);
                    }
                }
                catch (const std::exception& error) {
                    if (!stopRequested_.load(std::memory_order_acquire)) {
                        LogWarn(error.what());
                        SleepUnlessStopped(
                            stopRequested_,
                            std::chrono::seconds(2));
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
            running_.store(false, std::memory_order_release);
        }
        stoppedCondition_.notify_all();
    }

    void PubSubServer::stop()
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            stopRequested_.store(true, std::memory_order_release);
        }

        std::unique_lock<std::mutex> lock(stateMutex_);
        stoppedCondition_.wait(lock, [this]() {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

#if defined(_WIN32)
#define PUBSUB_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PUBSUB_PLUGIN_EXPORT extern "C"
#endif

PUBSUB_PLUGIN_EXPORT GraftcodeGateway::IServer* CreateServer()
{
    return new Graftcode::Plugins::PubSub::PubSubServer();
}

PUBSUB_PLUGIN_EXPORT void DestroyServer(GraftcodeGateway::IServer* server)
{
    delete server;
}
