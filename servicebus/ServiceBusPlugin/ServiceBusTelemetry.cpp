#include "ServiceBusTelemetry.h"

#include <azure/core/amqp/models/amqp_value.hpp>

#include <chrono>
#include <mutex>
#include <regex>
#include <thread>

namespace Graftcode::Plugins::ServiceBus::Telemetry
{
	namespace
	{
		constexpr const char* kTraceParentProperty = "traceparent";
		constexpr const char* kTraceStateProperty = "tracestate";
		constexpr const char* kDiagnosticIdProperty = "Diagnostic-Id";

		thread_local InvocationContext g_invocationContext;
		thread_local std::string g_lastSpanJson;

		std::mutex g_lastSpanMutex;
		std::string g_lastSpanJsonGlobal;

		std::string escapeJson(const std::string& value)
		{
			std::string escaped;
			escaped.reserve(value.size());
			for (const char c : value) {
				switch (c) {
				case '\\': escaped += "\\\\"; break;
				case '"': escaped += "\\\""; break;
				case '\n': escaped += "\\n"; break;
				case '\r': escaped += "\\r"; break;
				case '\t': escaped += "\\t"; break;
				default: escaped += c; break;
				}
			}
			return escaped;
		}

		std::string serializeSpan(const TransportSpan& span)
		{
			std::string json = "{";
			json += "\"transport\":\"" + escapeJson(span.transport) + "\"";
			json += ",\"operation\":\"" + escapeJson(span.operation) + "\"";
			json += ",\"queue\":\"" + escapeJson(span.queue) + "\"";
			if (!span.replyQueue.empty()) {
				json += ",\"replyQueue\":\"" + escapeJson(span.replyQueue) + "\"";
			}
			if (!span.correlationId.empty()) {
				json += ",\"correlationId\":\"" + escapeJson(span.correlationId) + "\"";
			}
			json += ",\"durationMs\":" + std::to_string(span.durationMs);
			json += ",\"success\":" + std::string(span.success ? "true" : "false");
			if (!span.traceparent.empty()) {
				json += ",\"traceparent\":\"" + escapeJson(span.traceparent) + "\"";
			}
			if (!span.tracestate.empty()) {
				json += ",\"tracestate\":\"" + escapeJson(span.tracestate) + "\"";
			}
			json += "}";
			return json;
		}

		std::optional<std::string> readStringProperty(
			const Azure::Core::Amqp::Models::AmqpMessage& message,
			const char* key)
		{
			const auto it = message.ApplicationProperties.find(key);
			if (it == message.ApplicationProperties.end()) {
				return std::nullopt;
			}

			try {
				const std::string value = static_cast<std::string>(it->second);
				if (value.empty()) {
					return std::nullopt;
				}
				return value;
			}
			catch (...) {
				return std::nullopt;
			}
		}
	}

	bool IsValidTraceParent(const std::string& traceparent)
	{
		static const std::regex pattern(
			R"(^00-[0-9a-f]{32}-[0-9a-f]{16}-[0-9a-f]{2}$)",
			std::regex::icase);
		return std::regex_match(traceparent, pattern);
	}

	void SetInvocationContext(const std::string& traceparent, const std::string& tracestate)
	{
		g_invocationContext.traceparent = traceparent;
		g_invocationContext.tracestate = tracestate;
	}

	void ClearInvocationContext()
	{
		g_invocationContext = {};
	}

	std::optional<InvocationContext> CurrentInvocationContext()
	{
		if (g_invocationContext.traceparent.empty()) {
			return std::nullopt;
		}
		return g_invocationContext;
	}

	void ApplyTraceContextToMessage(Azure::Core::Amqp::Models::AmqpMessage& message)
	{
		const auto context = CurrentInvocationContext();
		if (!context.has_value()) {
			return;
		}

		if (!context->traceparent.empty() && IsValidTraceParent(context->traceparent)) {
			message.ApplicationProperties[kTraceParentProperty] =
				Azure::Core::Amqp::Models::AmqpValue(context->traceparent);
			// Azure SDK and legacy Application Insights consumers.
			message.ApplicationProperties[kDiagnosticIdProperty] =
				Azure::Core::Amqp::Models::AmqpValue(context->traceparent);
		}

		if (!context->tracestate.empty()) {
			message.ApplicationProperties[kTraceStateProperty] =
				Azure::Core::Amqp::Models::AmqpValue(context->tracestate);
		}
	}

	std::optional<InvocationContext> ExtractTraceContextFromMessage(
		const Azure::Core::Amqp::Models::AmqpMessage& message)
	{
		InvocationContext context;

		if (const auto traceparent = readStringProperty(message, kTraceParentProperty)) {
			context.traceparent = *traceparent;
		}
		else if (const auto diagnosticId = readStringProperty(message, kDiagnosticIdProperty)) {
			context.traceparent = *diagnosticId;
		}

		if (const auto tracestate = readStringProperty(message, kTraceStateProperty)) {
			context.tracestate = *tracestate;
		}

		if (context.traceparent.empty()) {
			return std::nullopt;
		}

		return context;
	}

	void RecordTransportSpan(const TransportSpan& span)
	{
		const std::string json = serializeSpan(span);
		g_lastSpanJson = json;
		std::lock_guard<std::mutex> lock(g_lastSpanMutex);
		g_lastSpanJsonGlobal = json;
	}

	std::string LastTransportSpanJson()
	{
		std::lock_guard<std::mutex> lock(g_lastSpanMutex);
		if (!g_lastSpanJsonGlobal.empty()) {
			return g_lastSpanJsonGlobal;
		}
		return g_lastSpanJson;
	}

	void ClearLastTransportSpan()
	{
		g_lastSpanJson.clear();
		std::lock_guard<std::mutex> lock(g_lastSpanMutex);
		g_lastSpanJsonGlobal.clear();
	}
}

#if defined(_WIN32)
#define SERVICEBUS_TELEMETRY_EXPORT extern "C" __declspec(dllexport)
#else
#define SERVICEBUS_TELEMETRY_EXPORT extern "C"
#endif

SERVICEBUS_TELEMETRY_EXPORT void SetServiceBusInvocationContext(
	const char* traceparent,
	const char* tracestate)
{
	Graftcode::Plugins::ServiceBus::Telemetry::SetInvocationContext(
		traceparent != nullptr ? std::string(traceparent) : std::string(),
		tracestate != nullptr ? std::string(tracestate) : std::string());
}

SERVICEBUS_TELEMETRY_EXPORT void ClearServiceBusInvocationContext()
{
	Graftcode::Plugins::ServiceBus::Telemetry::ClearInvocationContext();
}

SERVICEBUS_TELEMETRY_EXPORT const char* GetServiceBusLastTransportTelemetryJson()
{
	static thread_local std::string cachedJson;
	cachedJson = Graftcode::Plugins::ServiceBus::Telemetry::LastTransportSpanJson();
	return cachedJson.c_str();
}

SERVICEBUS_TELEMETRY_EXPORT const char* GetServiceBusCurrentInvocationContextJson()
{
	static thread_local std::string cachedJson;
	const auto context = Graftcode::Plugins::ServiceBus::Telemetry::CurrentInvocationContext();
	if (!context.has_value()) {
		cachedJson.clear();
		return cachedJson.c_str();
	}

	cachedJson = "{\"traceparent\":\"" + context->traceparent + "\"";
	if (!context->tracestate.empty()) {
		cachedJson += ",\"tracestate\":\"" + context->tracestate + "\"";
	}
	cachedJson += "}";
	return cachedJson.c_str();
}
