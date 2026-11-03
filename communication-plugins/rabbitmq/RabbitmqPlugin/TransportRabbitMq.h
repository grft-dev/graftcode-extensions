#pragma once

#include "ITransport.h"
#include "RabbitMqClient.h"

#include <memory>
#include <string>
#include <vector>

namespace Graftcode::Plugins::Rabbitmq
{
	class TransportRabbitMq : public Hypertube::Native::Interfaces::ITransport
	{
	public:
		TransportRabbitMq(std::string host, unsigned short port, std::string configSource);
		~TransportRabbitMq() override = default;

		int Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion) override;
		std::vector<byte> SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) override;

	private:
		RabbitMqClientConfig config_;
		std::string configSource;
		std::unique_ptr<RabbitMqRpcClient> client_;
	};
}
