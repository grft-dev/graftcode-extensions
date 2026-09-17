#pragma once

#include "SqsConfig.h"

#include <cstddef>
#include <memory>
#include <mutex>
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
        SqsConfig config_;
        std::unique_ptr<Aws::SQS::SQSClient> client_;
        std::mutex callMutex_;
    };
}
