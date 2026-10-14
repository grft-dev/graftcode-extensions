#pragma once

#include "PendingReplies.h"
#include "PubSubConfig.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
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
        void EnsureStarted();
        void DispatchReplies();
        std::unique_ptr<PubSubSession> AcquireSession();
        void ReleaseSession(std::unique_ptr<PubSubSession> session);

        PubSubConfig config_;
        Concurrency::PendingReplies pending_;
        std::unique_ptr<PubSubSession> replySession_;
        std::vector<std::unique_ptr<PubSubSession>> idleSessions_;
        std::mutex sessionsMutex_;
        std::thread dispatcher_;
        std::mutex startMutex_;
        std::atomic<bool> started_{ false };
        std::atomic<bool> stopping_{ false };
    };
}
