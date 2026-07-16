#pragma once

#include <cstddef>

namespace GraftcodeGateway
{
	/**
	 * Optional server-side invocation context passed with each broker message.
	 * Proposed extension for IServer::ProcessMessageFn.
	 */
	struct ServerInvocationContext
	{
		const char* traceparent{ nullptr };
		const char* tracestate{ nullptr };
		const char* correlationId{ nullptr };
		const char* replyQueue{ nullptr };
		const char* replySessionId{ nullptr };
	};

	/**
	 * Proposed replacement for ProcessMessageFn when telemetry-aware hosting is enabled.
	 *
	 * The gateway (gg) should:
	 * 1. Read broker trace context from the plugin.
	 * 2. Populate RequestContext / graft headers before invoking user code.
	 * 3. Emit a server-side dependency span using returned transport metadata.
	 */
	using ProcessMessageWithContextFn = bool(*)(
		const unsigned char* requestData,
		std::size_t requestSize,
		const ServerInvocationContext* invocationContext,
		void (*writeResponse)(void* context, const unsigned char* data, std::size_t size),
		void* writeContext);
}
