#pragma once

#include "ITransport.h"
#include "PubSubClient.h"

#include <memory>
#include <vector>

namespace Graftcode::Plugins::PubSub
{
    class TransportPubSub final
        : public Hypertube::Native::Interfaces::ITransport
    {
    public:
        explicit TransportPubSub(const char* configSource);
        ~TransportPubSub() override = default;

        int Initialize(
            byte callingRuntimeNumber,
            byte calledRuntimeNumber,
            byte calledRuntimeVersion) override;
        std::vector<byte> SendCommand(
            byte* messageByteArray,
            int32_t messageByteArrayLen) override;

    private:
        std::unique_ptr<PubSubClient> client_;
    };

    void RejectGatewayErrorPayload(const std::vector<unsigned char>& payload);
}
