#include "TransportSqs.h"

#include "SqsConfig.h"

#include <algorithm>
#include <stdexcept>
#include <string>

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

int TransportSqs::SendCommand(
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
    if (!response.empty() && response.front() == static_cast<byte>(255)) {
        throw std::runtime_error(std::string(response.begin() + 1, response.end()));
    }

    const int responseSize = static_cast<int>(response.size());
    {
        std::lock_guard<std::mutex> lock(responsesMutex_);
        responses_[std::this_thread::get_id()] = std::move(response);
    }
    return responseSize;
}

int TransportSqs::ReadResponse(
    byte* responseByteArray,
    int32_t responseByteArrayLen)
{
    if (responseByteArrayLen < 0 ||
        (responseByteArrayLen > 0 && responseByteArray == nullptr)) {
        throw std::invalid_argument("SQS transport: invalid response buffer");
    }

    std::lock_guard<std::mutex> lock(responsesMutex_);
    const auto response = responses_.find(std::this_thread::get_id());
    if (response == responses_.end()) {
        throw std::runtime_error(
            "SQS transport: response not found for calling thread");
    }
    if (responseByteArrayLen !=
        static_cast<int32_t>(response->second.size())) {
        throw std::runtime_error(
            "SQS transport: response buffer length mismatch");
    }

    std::copy(
        response->second.begin(),
        response->second.end(),
        responseByteArray);
    responses_.erase(response);
    return 0;
}

#if defined(_WIN32)
#define SQS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SQS_PLUGIN_EXPORT extern "C"
#endif

SQS_PLUGIN_EXPORT Hypertube::Native::Interfaces::ITransport*
CreateTransportChannel(
    const char*,
    const unsigned short,
    const char* configSource)
{
    return new TransportSqs(configSource);
}

SQS_PLUGIN_EXPORT void DestroyTransportChannel(
    Hypertube::Native::Interfaces::ITransport* transport)
{
    delete transport;
}
