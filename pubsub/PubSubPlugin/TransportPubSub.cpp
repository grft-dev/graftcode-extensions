#include "TransportPubSub.h"

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

    int TransportPubSub::SendCommand(
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

        const int responseSize = static_cast<int>(response.size());
        {
            std::lock_guard<std::mutex> lock(responsesMutex_);
            responses_[std::this_thread::get_id()] = std::move(response);
        }
        return responseSize;
    }

    int TransportPubSub::ReadResponse(
        byte* responseByteArray,
        int32_t responseByteArrayLen)
    {
        if (responseByteArrayLen < 0 ||
            (responseByteArrayLen > 0 && responseByteArray == nullptr)) {
            throw std::invalid_argument(
                "Pub/Sub transport: invalid response buffer");
        }

        std::lock_guard<std::mutex> lock(responsesMutex_);
        const auto response = responses_.find(std::this_thread::get_id());
        if (response == responses_.end()) {
            throw std::runtime_error(
                "Pub/Sub transport: response not found for calling thread");
        }
        if (responseByteArrayLen !=
            static_cast<int32_t>(response->second.size())) {
            throw std::runtime_error(
                "Pub/Sub transport: response buffer length mismatch");
        }

        std::copy(
            response->second.begin(),
            response->second.end(),
            responseByteArray);
        responses_.erase(response);
        return 0;
    }
}

#if defined(_WIN32)
#define PUBSUB_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PUBSUB_PLUGIN_EXPORT extern "C"
#endif

PUBSUB_PLUGIN_EXPORT Hypertube::Native::Interfaces::ITransport*
CreateTransportChannel(
    const char*,
    const unsigned short,
    const char* configSource)
{
    return new Graftcode::Plugins::PubSub::TransportPubSub(configSource);
}

PUBSUB_PLUGIN_EXPORT void DestroyTransportChannel(
    Hypertube::Native::Interfaces::ITransport* transport)
{
    delete transport;
}
