#include "TransportSqs.h"
#include "TransportPluginExports.h"

#include "SqsConfig.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Graftcode::Plugins::Sqs
{
    void RejectGatewayErrorPayload(const std::vector<unsigned char>& payload)
    {
        if (!payload.empty() &&
            payload.front() == static_cast<unsigned char>(255)) {
            throw std::runtime_error(
                std::string(payload.begin() + 1, payload.end()));
        }
    }
}

using Graftcode::Plugins::Sqs::RejectGatewayErrorPayload;
using Graftcode::Plugins::Sqs::SqsClient;
using Graftcode::Plugins::Sqs::TransportSqs;

TransportSqs::TransportSqs(const char* configSource)
    : client_(std::make_unique<SqsClient>(
        Graftcode::Plugins::Sqs::ParseSqsConfigSource(
            configSource != nullptr ? configSource : "")))
{
}

int TransportSqs::Initialize(byte, byte, byte)
{
    return 0;
}

std::vector<byte> TransportSqs::SendCommand(
    byte* messageByteArray,
    int32_t messageByteArrayLen)
{
    if (messageByteArrayLen < 0 ||
        (messageByteArrayLen > 0 && messageByteArray == nullptr)) {
        throw std::invalid_argument("SQS transport: invalid request payload");
    }

    std::vector<byte> response = client_->Call(
        messageByteArray,
        static_cast<std::size_t>(messageByteArrayLen));
    RejectGatewayErrorPayload(response);

    return response;
}

#if defined(_WIN32)
#define SQS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SQS_PLUGIN_EXPORT extern "C"
#endif

SQS_PLUGIN_EXPORT TransportPluginChannel*
CreateTransport(
    const char*,
    unsigned short,
    const char* configSource,
    char** errorMessage)
{
    return Graftcode::Plugins::TransportAbi::CreateTransport(
        [&] { return new TransportSqs(configSource); },
        errorMessage);
}

SQS_PLUGIN_EXPORT void DestroyTransport(
    TransportPluginChannel* channel)
{
    Graftcode::Plugins::TransportAbi::DestroyTransport(channel);
}

SQS_PLUGIN_EXPORT int32_t TransportSendCommand(
    TransportPluginChannel* channel,
    const unsigned char* message,
    int32_t messageLen,
    unsigned char** response,
    int32_t* responseLen)
{
    return Graftcode::Plugins::TransportAbi::SendCommand(
        channel, message, messageLen, response, responseLen);
}

SQS_PLUGIN_EXPORT void TransportFreeBuffer(void* buffer)
{
    Graftcode::Plugins::TransportAbi::FreeBuffer(buffer);
}
