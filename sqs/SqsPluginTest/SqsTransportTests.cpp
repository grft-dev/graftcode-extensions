#include <gtest/gtest.h>

#include "IServer.h"
#include "ITransport.h"
#include "SqsClient.h"
#include "SqsConfig.h"
#include "SqsServer.h"
#include "SqsUtil.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern "C" Hypertube::Native::Interfaces::ITransport*
CreateTransportChannel(
    const char* ipAddress,
    unsigned short port,
    const char* configSource);
extern "C" void DestroyTransportChannel(
    Hypertube::Native::Interfaces::ITransport* transport);

using Graftcode::Plugins::Sqs::DecodePayload;
using Graftcode::Plugins::Sqs::EncodePayload;
using Graftcode::Plugins::Sqs::IsFifoQueueUrl;
using Graftcode::Plugins::Sqs::ParseSqsConfigSource;
using Graftcode::Plugins::Sqs::SqsClient;
using Graftcode::Plugins::Sqs::SqsConfig;
using Graftcode::Plugins::Sqs::SqsServer;
using Graftcode::Plugins::Sqs::ValidateClientConfig;

namespace
{
    const char* kLocalConfig = R"({
        "name": "SqsPlugin",
        "region": "us-east-1",
        "endpointOverride": "http://localhost:4566",
        "accessKeyId": "test",
        "secretAccessKey": "test",
        "requestQueueUrl": "http://localhost:4566/000000000000/graft-requests",
        "replyQueueUrl": "http://localhost:4566/000000000000/graft-replies",
        "rpcTimeoutMs": 5000,
        "waitTimeSeconds": 1,
        "visibilityTimeoutSeconds": 30,
        "verifySsl": false
    })";

    const std::vector<unsigned char> kPayload{
        0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff
    };

    bool EchoCallback(
        const unsigned char* data,
        std::size_t size,
        GraftcodeGateway::IServer::WriteResponseFn writeResponse,
        void* context)
    {
        writeResponse(context, data, size);
        return true;
    }

    std::string EnvOr(const char* name, const char* fallback)
    {
        const char* value = std::getenv(name);
        return value != nullptr ? value : fallback;
    }

    std::string EscapeJson(const std::string& value)
    {
        std::string escaped;
        for (const char character : value) {
            if (character == '\\' || character == '"') {
                escaped.push_back('\\');
            }
            escaped.push_back(character);
        }
        return escaped;
    }
}

TEST(SqsConfig, ParsesQueueAndAwsSettings)
{
    const SqsConfig config = ParseSqsConfigSource(kLocalConfig);

    EXPECT_EQ(config.region, "us-east-1");
    EXPECT_EQ(
        config.requestQueueUrl,
        "http://localhost:4566/000000000000/graft-requests");
    EXPECT_EQ(
        config.replyQueueUrl,
        "http://localhost:4566/000000000000/graft-replies");
    EXPECT_EQ(config.rpcTimeoutMs, 5000u);
    EXPECT_FALSE(config.verifySsl);
    EXPECT_NO_THROW(ValidateClientConfig(config));
}

TEST(SqsConfig, FindsNestedSqsNode)
{
    const SqsConfig config = ParseSqsConfigSource(R"({
        "configurations": {
            "package": {
                "plugin": {
                    "sqs": {
                        "region": "eu-west-1",
                        "requestQueueUrl": "https://example/requests",
                        "replyQueueUrl": "https://example/replies"
                    }
                }
            }
        }
    })");

    EXPECT_EQ(config.region, "eu-west-1");
    EXPECT_EQ(config.requestQueueUrl, "https://example/requests");
}

TEST(SqsConfig, RequiresReplyQueueForRpc)
{
    SqsConfig config;
    config.requestQueueUrl = "https://example/requests";
    EXPECT_THROW(ValidateClientConfig(config), std::runtime_error);

    config.oneWay = true;
    EXPECT_NO_THROW(ValidateClientConfig(config));
}

TEST(SqsPayload, PreservesArbitraryBinaryData)
{
    const Aws::String encoded = EncodePayload(kPayload.data(), kPayload.size());
    EXPECT_EQ(DecodePayload(encoded), kPayload);
}

TEST(SqsPayload, SupportsEmptyPayload)
{
    const Aws::String encoded = EncodePayload(nullptr, 0);
    EXPECT_FALSE(encoded.empty());
    EXPECT_TRUE(DecodePayload(encoded).empty());
}

TEST(SqsConfig, DetectsFifoQueueUrls)
{
    EXPECT_TRUE(IsFifoQueueUrl("https://sqs.us-east-1.amazonaws.com/1/a.fifo"));
    EXPECT_TRUE(IsFifoQueueUrl("https://localhost/a.fifo/"));
    EXPECT_TRUE(IsFifoQueueUrl("https://localhost/a.fifo?x=1"));
    EXPECT_FALSE(IsFifoQueueUrl("https://sqs.us-east-1.amazonaws.com/1/a"));
}

TEST(SqsTransport, FactoryCreatesAndDestroysTransport)
{
    auto* transport = CreateTransportChannel("", 0, kLocalConfig);
    ASSERT_NE(transport, nullptr);
    DestroyTransportChannel(transport);
}

TEST(SqsLive, RpcRoundTrip)
{
    const char* requestQueue = std::getenv("SQS_REQUEST_QUEUE_URL");
    const char* replyQueue = std::getenv("SQS_REPLY_QUEUE_URL");
    if (requestQueue == nullptr || replyQueue == nullptr) {
        GTEST_SKIP()
            << "SQS_REQUEST_QUEUE_URL and SQS_REPLY_QUEUE_URL are not set";
    }

    const std::string region = EnvOr("AWS_REGION", "us-east-1");
    const std::string endpoint = EnvOr("SQS_ENDPOINT_OVERRIDE", "");
    const std::string accessKey = EnvOr("AWS_ACCESS_KEY_ID", "");
    const std::string secretKey = EnvOr("AWS_SECRET_ACCESS_KEY", "");
    const std::string sessionToken = EnvOr("AWS_SESSION_TOKEN", "");

    const std::string json =
        std::string("{\"region\":\"") + EscapeJson(region) +
        "\",\"requestQueueUrl\":\"" + EscapeJson(requestQueue) +
        "\",\"replyQueueUrl\":\"" + EscapeJson(replyQueue) +
        "\",\"endpointOverride\":\"" + EscapeJson(endpoint) +
        "\",\"accessKeyId\":\"" + EscapeJson(accessKey) +
        "\",\"secretAccessKey\":\"" + EscapeJson(secretKey) +
        "\",\"sessionToken\":\"" + EscapeJson(sessionToken) +
        "\",\"rpcTimeoutMs\":20000,\"waitTimeSeconds\":1}";

    SqsServer server;
    server.configure(json.c_str(), &EchoCallback);
    std::thread serverThread([&server]() {
        try {
            server.start();
        }
        catch (...) {
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::vector<unsigned char> response;
    std::exception_ptr callError;
    try {
        SqsClient client(ParseSqsConfigSource(json));
        response = client.Call(kPayload.data(), kPayload.size());
    }
    catch (...) {
        callError = std::current_exception();
    }

    server.stop();
    serverThread.join();
    if (callError) {
        std::rethrow_exception(callError);
    }
    EXPECT_EQ(response, kPayload);
}
