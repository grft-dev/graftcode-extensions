#pragma once

#include "ITransport.h"
#include "ServiceBusClient.h"

#include <string>
#include <vector>

namespace Graftcode::Plugins::ServiceBus
{
	class TransportServiceBus : public Hypertube::Native::Interfaces::ITransport
	{
	public:
		TransportServiceBus(std::string host, unsigned short port, std::string configSource);
		~TransportServiceBus() override = default;

		int Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion) override;
		std::vector<byte> SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) override;

	private:
		ServiceBusClientConfig config_;
		std::string configSource;
	};
}
