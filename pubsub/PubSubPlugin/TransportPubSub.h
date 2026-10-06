#pragma once

#include "ITransport.h"
#include "PubSubClient.h"

#include <map>
#include <memory>
#include <mutex>
#include <thread>
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
        int SendCommand(
            byte* messageByteArray,
            int32_t messageByteArrayLen) override;
        int ReadResponse(
            byte* responseByteArray,
            int32_t responseByteArrayLen) override;

    private:
        std::unique_ptr<PubSubClient> client_;
        std::map<std::thread::id, std::vector<byte>> responses_;
        std::mutex responsesMutex_;
    };

    void RejectGatewayErrorPayload(const std::vector<unsigned char>& payload);
}
