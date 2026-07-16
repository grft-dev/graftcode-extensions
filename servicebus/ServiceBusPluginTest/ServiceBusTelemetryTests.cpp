#include <gtest/gtest.h>

#include "ServiceBusTelemetry.h"

using Graftcode::Plugins::ServiceBus::Telemetry::ClearInvocationContext;
using Graftcode::Plugins::ServiceBus::Telemetry::CurrentInvocationContext;
using Graftcode::Plugins::ServiceBus::Telemetry::IsValidTraceParent;
using Graftcode::Plugins::ServiceBus::Telemetry::LastTransportSpanJson;
using Graftcode::Plugins::ServiceBus::Telemetry::RecordTransportSpan;
using Graftcode::Plugins::ServiceBus::Telemetry::SetInvocationContext;
using Graftcode::Plugins::ServiceBus::Telemetry::TransportSpan;

TEST(ServiceBusTelemetryTests, ValidatesW3CTraceParent)
{
	EXPECT_TRUE(IsValidTraceParent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));
	EXPECT_FALSE(IsValidTraceParent("not-a-trace"));
	EXPECT_FALSE(IsValidTraceParent(""));
}

TEST(ServiceBusTelemetryTests, RecordsTransportSpanJson)
{
	ClearInvocationContext();
	SetInvocationContext("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01", "");

	TransportSpan span;
	span.operation = "rpc";
	span.queue = "queue-01";
	span.replyQueue = "queue-02";
	span.correlationId = "corr-1";
	span.durationMs = 15;
	span.success = true;
	span.traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
	RecordTransportSpan(span);

	const std::string json = LastTransportSpanJson();
	EXPECT_NE(json.find("\"transport\":\"azure.servicebus\""), std::string::npos);
	EXPECT_NE(json.find("\"queue\":\"queue-01\""), std::string::npos);
	EXPECT_NE(json.find("\"replyQueue\":\"queue-02\""), std::string::npos);
	EXPECT_NE(json.find("\"correlationId\":\"corr-1\""), std::string::npos);
	EXPECT_NE(json.find("\"durationMs\":15"), std::string::npos);
	EXPECT_NE(json.find("\"success\":true"), std::string::npos);
}

TEST(ServiceBusTelemetryTests, StoresInvocationContextPerThread)
{
	SetInvocationContext("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01", "vendor=value");
	const auto context = CurrentInvocationContext();
	ASSERT_TRUE(context.has_value());
	EXPECT_EQ(context->traceparent, "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
	EXPECT_EQ(context->tracestate, "vendor=value");
}
