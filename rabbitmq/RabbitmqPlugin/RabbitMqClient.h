#pragma once

#include "PendingReplies.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

typedef unsigned char byte;

namespace Graftcode::Plugins::Rabbitmq
{
	struct RabbitMqClientConfig
	{
		std::string host{ "" };
		std::uint16_t port{  };
		std::string queue{ "" };
		std::string user{ "" };
		std::string password{ "" };
		std::string vhost{ "" };
		std::uint32_t rpcTimeoutMs{ 30000 };
	};

	class RabbitMqClient
	{
	public:
		RabbitMqClient() = delete;
		~RabbitMqClient() = delete;
		static void ApplyConfigSource(const std::string& configSource, RabbitMqClientConfig& config);

		static bool Send(const RabbitMqClientConfig& config, const std::vector<byte>& payload);
		static bool Send(const RabbitMqClientConfig& config, const byte* payload, std::size_t payloadSize);
		static bool SendRpc(
			const RabbitMqClientConfig& config,
			const std::vector<byte>& payload,
			std::vector<byte>& responsePayload);
		static bool SendRpc(
			const RabbitMqClientConfig& config,
			const byte* payload,
			std::size_t payloadSize,
			std::vector<byte>& responsePayload);
	};

	// Request/reply client that keeps one connection open and lets Call run concurrently.
	// Replies arrive on a private reply queue and are matched by correlation id.
	class RabbitMqRpcClient
	{
	public:
		explicit RabbitMqRpcClient(RabbitMqClientConfig config);
		~RabbitMqRpcClient();

		RabbitMqRpcClient(const RabbitMqRpcClient&) = delete;
		RabbitMqRpcClient& operator=(const RabbitMqRpcClient&) = delete;

		std::vector<byte> Call(const byte* payload, std::size_t payloadSize);

	private:
		class Link;

		std::shared_ptr<Link> CurrentLink();

		RabbitMqClientConfig config_;
		Concurrency::PendingReplies pending_;
		std::mutex linkMutex_;
		std::shared_ptr<Link> link_;
	};
}
