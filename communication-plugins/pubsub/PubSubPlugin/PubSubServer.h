#pragma once

#include "IServer.h"
#include "PubSubConfig.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace Graftcode::Plugins::PubSub
{
    class PubSubServer final : public GraftcodeGateway::IServer
    {
    public:
        PubSubServer() = default;
        ~PubSubServer() override;

        void configure(
            const char* jsonConfig,
            ProcessMessageFn processMessage) override;
        void start() override;
        void stop() override;

    private:
        PubSubConfig config_;
        ProcessMessageFn processMessage_{ nullptr };
        std::mutex stateMutex_;
        std::condition_variable stoppedCondition_;
        std::atomic_bool stopRequested_{ false };
        std::atomic_bool running_{ false };
    };
}
