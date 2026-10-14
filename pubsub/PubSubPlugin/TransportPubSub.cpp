#include "TransportPubSub.h"
#include "TransportPluginExports.h"

#include "PubSubConfig.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Graftcode::Plugins::PubSub
{
    void RejectGatewayErrorPayload(const std::vector<unsigned char>& payload)
    {
        if (!payload.empty() &&
            payload.front() == static_cast<unsigned char>(255)) {
            throw std::runtime_error(
                std::string(payload.begin() + 1, payload.end()));
        }
    }

    TransportPubSub::TransportPubSub(const char* configSource)
        : client_(std::make_unique<PubSubClient>(ParsePubSubConfigSource(
            configSource != nullptr ? configSource : "")))
    {
    }

    int TransportPubSub::Initialize(byte, byte, byte)
    {
        return 0;
    }

    std::vector<byte> TransportPubSub::SendCommand(
        byte* messageByteArray,
        int32_t messageByteArrayLen)
    {
        if (messageByteArrayLen < 0 ||
            (messageByteArrayLen > 0 && messageByteArray == nullptr)) {
            throw std::invalid_argument(
                "Pub/Sub transport: invalid request payload");
        }

        std::vector<byte> response = client_->Call(
            messageByteArray,
            static_cast<std::size_t>(messageByteArrayLen));
        RejectGatewayErrorPayload(response);

        return response;
    }
}

#if defined(_WIN32)
#define PUBSUB_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PUBSUB_PLUGIN_EXPORT extern "C"
#endif

PUBSUB_PLUGIN_EXPORT GraftcodeTransportChannel*
CreateTransport(
    const char*,
    unsigned short,
    const char* configSource,
    char** errorMessage)
{
    return Graftcode::Plugins::TransportAbi::CreateTransport(
        [&] { return new Graftcode::Plugins::PubSub::TransportPubSub(configSource); },
        errorMessage);
}

PUBSUB_PLUGIN_EXPORT void DestroyTransport(
    GraftcodeTransportChannel* channel)
{
    Graftcode::Plugins::TransportAbi::DestroyTransport(channel);
}

PUBSUB_PLUGIN_EXPORT int32_t TransportSendCommand(
    GraftcodeTransportChannel* channel,
    const unsigned char* message,
    int32_t messageLen,
    unsigned char** response,
    int32_t* responseLen)
{
    return Graftcode::Plugins::TransportAbi::SendCommand(
        channel, message, messageLen, response, responseLen);
}

PUBSUB_PLUGIN_EXPORT void TransportFreeBuffer(void* buffer)
{
    Graftcode::Plugins::TransportAbi::FreeBuffer(buffer);
}
