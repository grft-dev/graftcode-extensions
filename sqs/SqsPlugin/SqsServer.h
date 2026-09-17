#pragma once

#include "IServer.h"
#include "SqsConfig.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace Aws::SQS
{
    class SQSClient;
}

namespace Graftcode::Plugins::Sqs
{
    class SqsServer final : public GraftcodeGateway::IServer
    {
    public:
        SqsServer() = default;
        ~SqsServer() override;

        void configure(
            const char* jsonConfig,
            ProcessMessageFn processMessage) override;
        void start() override;
        void stop() override;

    private:
        SqsConfig config_;
        ProcessMessageFn processMessage_{ nullptr };
        std::mutex stateMutex_;
        std::condition_variable stoppedCondition_;
        Aws::SQS::SQSClient* activeClient_{ nullptr };
        std::atomic_bool stopRequested_{ false };
        std::atomic_bool running_{ false };
    };
}
