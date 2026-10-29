#pragma once

#include "ITransport.h"
#include "SqsClient.h"

#include <memory>
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
        std::vector<byte> SendCommand(
            byte* messageByteArray,
            int32_t messageByteArrayLen) override;

    private:
        std::unique_ptr<SqsClient> client_;
    };

    void RejectGatewayErrorPayload(const std::vector<unsigned char>& payload);
}
