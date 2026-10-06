#pragma once

#include "PubSubConfig.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace Graftcode::Plugins::PubSub
{
    class PubSubSession;

    class PubSubClient
    {
    public:
        explicit PubSubClient(PubSubConfig config);
        ~PubSubClient();

        PubSubClient(const PubSubClient&) = delete;
        PubSubClient& operator=(const PubSubClient&) = delete;

        std::vector<unsigned char> Call(
            const unsigned char* data,
            std::size_t size);

    private:
        void EnsureResources();

        PubSubConfig config_;
        std::unique_ptr<PubSubSession> session_;
        bool resourcesReady_{ false };
        std::mutex callMutex_;
    };
}
