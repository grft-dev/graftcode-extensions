#pragma once

#include <azure/core/amqp/models/amqp_message.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace Graftcode::Plugins::ServiceBus::Telemetry
{
	struct InvocationContext
	{
		std::string traceparent;
		std::string tracestate;
	};

	struct TransportSpan
	{
		std::string transport{ "azure.servicebus" };
		std::string operation;
		std::string queue;
		std::string replyQueue;
		std::string correlationId;
		std::int64_t durationMs{ 0 };
		bool success{ false };
		std::string traceparent;
		std::string tracestate;
	};

	void SetInvocationContext(const std::string& traceparent, const std::string& tracestate);
	void ClearInvocationContext();
	std::optional<InvocationContext> CurrentInvocationContext();

	void ApplyTraceContextToMessage(Azure::Core::Amqp::Models::AmqpMessage& message);
	std::optional<InvocationContext> ExtractTraceContextFromMessage(
		const Azure::Core::Amqp::Models::AmqpMessage& message);

	void RecordTransportSpan(const TransportSpan& span);
	std::string LastTransportSpanJson();
	void ClearLastTransportSpan();

	bool IsValidTraceParent(const std::string& traceparent);
}
