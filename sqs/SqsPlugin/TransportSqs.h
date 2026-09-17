#pragma once

#include "ITransport.h"
#include "SqsClient.h"

#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Graftcode::Plugins::Sqs
{
    class TransportSqs final
        : public Hypertube::Native::Interfaces::ITransport
    {
    public:
        explicit TransportSqs(const char* configSource);
        ~TransportSqs() override = default;

        int Initialize(
            byte callingRuntimeNumber,
            byte calledRuntimeNumber,
            byte calledRuntimeVersion) override;
        int SendCommand(
            byte* messageByteArray,
            int32_t messageByteArrayLen) override;
        int ReadResponse(
            byte* responseByteArray,
            int32_t responseByteArrayLen) override;

    private:
        std::unique_ptr<SqsClient> client_;
        std::map<std::thread::id, std::vector<byte>> responses_;
        std::mutex responsesMutex_;
    };
}
