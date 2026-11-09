#pragma once

#include "PendingReplies.h"
#include "SqsConfig.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Aws::SQS
{
    class SQSClient;
}

namespace Graftcode::Plugins::Sqs
{
    class SqsClient
    {
    public:
        explicit SqsClient(SqsConfig config);
        ~SqsClient();

        SqsClient(const SqsClient&) = delete;
        SqsClient& operator=(const SqsClient&) = delete;

        std::vector<unsigned char> Call(
            const unsigned char* data,
            std::size_t size);

    private:
        void EnsureDispatcher();
        void DispatchReplies();

        SqsConfig config_;
        std::unique_ptr<Aws::SQS::SQSClient> client_;
        Concurrency::PendingReplies pending_;
        std::thread dispatcher_;
        std::mutex startMutex_;
        std::atomic<bool> started_{ false };
        std::atomic<bool> stopping_{ false };
    };
}
