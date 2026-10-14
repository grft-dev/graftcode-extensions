#include <gtest/gtest.h>

#include "ITransport.h"
#include "TransportPluginAbi.h"
#include "RabbitMqClient.h"
#include <map>
#include <shared_mutex>
#include <thread>
#include <mutex>
#include "TransportRabbitMq.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" GraftcodeTransportChannel* CreateTransport(
	const char* ipAddress,
	unsigned short port,
	const char* configSource,
	char** errorMessage);
extern "C" void DestroyTransport(GraftcodeTransportChannel* channel);
extern "C" int32_t TransportSendCommand(
	GraftcodeTransportChannel* channel,
	const unsigned char* message,
	int32_t messageLen,
	unsigned char** response,
	int32_t* responseLen);
extern "C" void TransportFreeBuffer(void* buffer);

namespace {
	const char* kValidClientConfig = R"({
			"host": "127.0.0.1",
			"port": 5672,
			"queue": "gg",
			"user": "guest",
			"password": "guest",
			"vhost": "/",
			"rpcTimeoutMs": 30000
})";

	const std::vector<byte> kSamplePayload{
		0x03, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x01, 0x28, 0x16, 0x08, 0x06, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x01, 0x13, 0x00, 0x00, 0x00,
		0x54, 0x65, 0x73, 0x74, 0x43, 0x6c, 0x61, 0x73, 0x73, 0x2e, 0x54, 0x65,
		0x73, 0x74, 0x43, 0x6c, 0x61, 0x73, 0x73
	};

	const std::vector<byte> kReadResponsePayload{
		0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x07, 0x01,
		0x01, 0x24, 0x00, 0x00, 0x00, 0x30, 0x39, 0x39, 0x34, 0x36, 0x36, 0x39,
		0x66, 0x2d, 0x32, 0x61, 0x31, 0x30, 0x2d, 0x34, 0x36, 0x65, 0x35, 0x2d,
		0x61, 0x39, 0x31, 0x61, 0x2d, 0x38, 0x38, 0x38, 0x34, 0x31, 0x63, 0x34,
		0x63, 0x33, 0x63, 0x62, 0x34
	};
}

void ExpectInvalidPayload(
	Graftcode::Plugins::Rabbitmq::TransportRabbitMq& transport,
	byte* payload,
	int32_t length)
{
	try {
		transport.SendCommand(payload, length);
		FAIL() << "SendCommand should reject invalid payload arguments";
	}
	catch (const std::runtime_error& error) {
		EXPECT_STREQ(error.what(), "invalid RabbitMQ payload");
	}
}

TEST(RabbitMqTransport, SendCommandRejectsInvalidPayloadArguments) {
	Graftcode::Plugins::Rabbitmq::TransportRabbitMq transport(
		"127.0.0.1",
		5672,
		kValidClientConfig);

	byte sample = 0x01;
	ExpectInvalidPayload(transport, nullptr, -1);
	ExpectInvalidPayload(transport, nullptr, 0);
	ExpectInvalidPayload(transport, nullptr, 8);
	ExpectInvalidPayload(transport, &sample, -5);

}

TEST(RabbitMqTransport, LiveRoundTrip) {
	if (std::getenv("RABBITMQ_LIVE") == nullptr) {
		GTEST_SKIP() << "RABBITMQ_LIVE is not set; broker round-trip is opt-in";
	}

	Graftcode::Plugins::Rabbitmq::TransportRabbitMq transport(
		"127.0.0.1",
		5672,
		kValidClientConfig);
	for (int i = 0; i < 300; ++i) {
		const std::vector<byte> actualResponse = transport.SendCommand(
			const_cast<byte*>(kSamplePayload.data()),
			static_cast<int32_t>(kSamplePayload.size()));
		EXPECT_EQ(actualResponse, kReadResponsePayload);
	}
}

