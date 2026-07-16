#pragma once

/**
 * Optional telemetry extension for Hypertube transport plugins.
 *
 * This header documents the contract implemented by ServiceBusPlugin today via
 * exported C functions. Hypertube can adopt the same shape as virtual methods
 * on a future ITransportTelemetry interface.
 */

namespace Hypertube::Native::Interfaces
{
	/**
	 * Per-invocation W3C trace context propagated from the calling runtime.
	 * Hypertube should populate this from thread-local graft headers
	 * (traceparent / tracestate) immediately before ITransport::SendCommand.
	 */
	struct TransportInvocationContext
	{
		const char* traceparent{ nullptr };
		const char* tracestate{ nullptr };
	};

	/**
	 * Transport-level span metadata returned after a plugin RPC completes.
	 * Hypertube can translate this into OpenTelemetry spans or
	 * Application Insights DependencyTelemetry.
	 */
	struct TransportTelemetrySpan
	{
		const char* transport{ nullptr };      // e.g. "azure.servicebus"
		const char* operation{ nullptr };      // e.g. "rpc", "process", "publish"
		const char* target{ nullptr };         // queue or topic name
		const char* replyTarget{ nullptr };    // reply queue when applicable
		const char* correlationId{ nullptr };
		const char* traceparent{ nullptr };
		const char* tracestate{ nullptr };
		long long durationMs{ 0 };
		bool success{ false };
	};

	/**
	 * Proposed optional extension for plugin transports.
	 *
	 * Existing plugins remain compatible when these methods are not implemented.
	 */
	class ITransportTelemetry
	{
	public:
		virtual ~ITransportTelemetry() = default;

		/**
		 * Called by Hypertube before SendCommand for the current thread.
		 */
		virtual void SetInvocationContext(const TransportInvocationContext& context) = 0;

		/**
		 * Called by Hypertube after SendCommand / ReadResponse complete.
		 * Returns JSON describing the last transport span for this thread.
		 */
		virtual const char* GetLastTransportTelemetryJson() = 0;

		virtual void ClearInvocationContext() = 0;
	};
}
