#include "TransportServiceBus.h"
#include "TransportPluginExports.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace Graftcode::Plugins::ServiceBus;


TransportServiceBus::TransportServiceBus(std::string host, unsigned short port, std::string configSource)
{
	(void)port;
	config_.host = host;
	this->configSource = configSource;
	ServiceBusClient::ApplyConfigSource(configSource, config_);
	client_ = std::make_unique<ServiceBusRpcClient>(config_);
}

int TransportServiceBus::Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion)
{
	(void)callingRuntimeNumber;
	(void)calledRuntimeNumber;
	(void)calledRuntimeVersion;
	return 0;
}

std::vector<byte> TransportServiceBus::SendCommand(byte* messageByteArray, int32_t messageByteArrayLen)
{
	if (messageByteArray == nullptr || messageByteArrayLen < 0) {
		throw std::runtime_error("invalid Service Bus payload");
	}

	std::vector<byte> responsePayload = client_->Call(
		messageByteArray,
		static_cast<std::size_t>(messageByteArrayLen));

	if (!responsePayload.empty() && responsePayload[0] == static_cast<byte>(255)) {
		const std::string errorMessage(responsePayload.begin() + 1, responsePayload.end());
		throw std::runtime_error(errorMessage);
	}

	return responsePayload;
}

#if defined(_WIN32)
#define SERVICEBUS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SERVICEBUS_PLUGIN_EXPORT extern "C"
#endif

SERVICEBUS_PLUGIN_EXPORT GraftcodeTransportChannel* CreateTransport(const char* ipAddress, unsigned short port, const char* configSource, char** errorMessage)
{
	return Graftcode::Plugins::TransportAbi::CreateTransport(
		[&] {
			return new TransportServiceBus(
				ipAddress != nullptr ? std::string(ipAddress) : std::string(""),
				port,
				configSource != nullptr ? std::string(configSource) : std::string(""));
		},
		errorMessage);
}

SERVICEBUS_PLUGIN_EXPORT void DestroyTransport(GraftcodeTransportChannel* channel)
{
	Graftcode::Plugins::TransportAbi::DestroyTransport(channel);
}

SERVICEBUS_PLUGIN_EXPORT int32_t TransportSendCommand(GraftcodeTransportChannel* channel, const unsigned char* message, int32_t messageLen, unsigned char** response, int32_t* responseLen)
{
	return Graftcode::Plugins::TransportAbi::SendCommand(channel, message, messageLen, response, responseLen);
}

SERVICEBUS_PLUGIN_EXPORT void TransportFreeBuffer(void* buffer)
{
	Graftcode::Plugins::TransportAbi::FreeBuffer(buffer);
}
