#include <gtest/gtest.h>

#include "IServer.h"
#include "ITransport.h"
#include "SqsClient.h"
#include "SqsConfig.h"
#include "SqsServer.h"
#include "SqsUtil.h"
#include "TransportSqs.h"

#include <aws/sqs/model/SendMessageRequest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
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
extern "C" GraftcodeGateway::IServer* CreateServer();
extern "C" void DestroyServer(GraftcodeGateway::IServer* server);

using Graftcode::Plugins::Sqs::ConfigureFifoMessage;
using Graftcode::Plugins::Sqs::DecodePayload;
using Graftcode::Plugins::Sqs::EncodePayload;
using Graftcode::Plugins::Sqs::IsFifoQueueUrl;
using Graftcode::Plugins::Sqs::MaxEncodedBodyBytes;
using Graftcode::Plugins::Sqs::ParseEndpointOverride;
using Graftcode::Plugins::Sqs::ParseSqsConfigSource;
using Graftcode::Plugins::Sqs::RejectGatewayErrorPayload;
using Graftcode::Plugins::Sqs::SqsClient;
using Graftcode::Plugins::Sqs::SqsConfig;
using Graftcode::Plugins::Sqs::SqsServer;
using Graftcode::Plugins::Sqs::ValidateClientConfig;
using Graftcode::Plugins::Sqs::ValidateServerConfig;

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

    std::atomic_bool g_oneWaySeen{ false };

    bool RecordOneWayCallback(
        const unsigned char* data,
        std::size_t size,
        GraftcodeGateway::IServer::WriteResponseFn,
        void*)
    {
        const bool matches =
            data != nullptr &&
            size == kPayload.size() &&
            std::equal(data, data + size, kPayload.begin());
        g_oneWaySeen.store(matches);
        return matches;
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
    EXPECT_FALSE(IsFifoQueueUrl("https://localhost/a.fifo.queue"));
}

TEST(SqsConfig, AcceptsFlatQueueAlias)
{
    const SqsConfig config = ParseSqsConfigSource(R"({
        "name": "SqsPlugin",
        "region": "eu-central-1",
        "queue": "https://example/requests",
        "replyQueue": "https://example/replies"
    })");

    EXPECT_EQ(config.requestQueueUrl, "https://example/requests");
    EXPECT_EQ(config.replyQueueUrl, "https://example/replies");
    EXPECT_EQ(config.region, "eu-central-1");
}

TEST(SqsConfig, ReadsNestedAliasAheadOfForeignQueue)
{
    const SqsConfig config = ParseSqsConfigSource(R"({
        "configurations": {
            "other": {
                "plugin": {
                    "name": "RabbitmqPlugin",
                    "queue": "myqueue",
                    "host": "localhost"
                }
            },
            "calculator": {
                "plugin": {
                    "name": "SqsPlugin",
                    "region": "us-west-2",
                    "queue": "https://example/requests",
                    "replyQueue": "https://example/replies"
                }
            }
        }
    })");

    EXPECT_EQ(config.region, "us-west-2");
    EXPECT_EQ(config.requestQueueUrl, "https://example/requests");
}

TEST(SqsConfig, RejectsForeignQueueConfig)
{
    EXPECT_THROW(
        ParseSqsConfigSource(R"({
            "name": "RabbitmqPlugin",
            "queue": "myqueue",
            "host": "localhost"
        })"),
        std::runtime_error);
}

TEST(SqsConfig, ReadsConfigurationFile)
{
    const auto path =
        std::filesystem::temp_directory_path() / "graftcode-sqs-config.json";
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.good());
        file << R"({
            "region": "ap-southeast-2",
            "queueUrl": "https://example/from-file",
            "replyQueueUrl": "https://example/from-file-replies",
            "oneWay": false
        })";
    }

    const SqsConfig config = ParseSqsConfigSource(path.string());
    EXPECT_EQ(config.region, "ap-southeast-2");
    EXPECT_EQ(config.requestQueueUrl, "https://example/from-file");
    std::filesystem::remove(path);
}

TEST(SqsConfig, RejectsInvalidValues)
{
    EXPECT_THROW(
        ParseSqsConfigSource(R"({"waitTimeSeconds": 21, "requestQueueUrl": "q"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParseSqsConfigSource(
            R"({"visibilityTimeoutSeconds": 43201, "requestQueueUrl": "q"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParseSqsConfigSource(
            R"({"accessKeyId": "only-one", "requestQueueUrl": "q"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParseSqsConfigSource(R"({"oneWay": "yes", "requestQueueUrl": "q"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParseSqsConfigSource(R"({"messageGroupId": ")" +
            std::string(129, 'g') +
            R"(", "requestQueueUrl": "q"})"),
        std::runtime_error);
    EXPECT_THROW(ParseSqsConfigSource(""), std::runtime_error);
    EXPECT_THROW(ParseSqsConfigSource("not-json-or-a-file"), std::runtime_error);
}

TEST(SqsConfig, ParsesEndpointOverride)
{
    const auto httpEndpoint = ParseEndpointOverride("http://localhost:4566/");
    EXPECT_TRUE(httpEndpoint.overridden);
    EXPECT_FALSE(httpEndpoint.useHttps);
    EXPECT_EQ(httpEndpoint.authority, "localhost:4566");

    const auto httpsEndpoint =
        ParseEndpointOverride(" https://sqs.example:443 ");
    EXPECT_TRUE(httpsEndpoint.useHttps);
    EXPECT_EQ(httpsEndpoint.authority, "sqs.example:443");

    EXPECT_FALSE(ParseEndpointOverride("").overridden);
    EXPECT_THROW(ParseEndpointOverride("http:///"), std::runtime_error);
}

TEST(SqsPayload, RejectsUnsupportedAndOversizedBodies)
{
    EXPECT_THROW(DecodePayload("not-a-graftcode-body"), std::runtime_error);
    EXPECT_THROW(DecodePayload("graftcode-base64:****"), std::runtime_error);
    EXPECT_THROW(DecodePayload("graftcode-base64:abc"), std::runtime_error);

    std::vector<unsigned char> oversized(MaxEncodedBodyBytes, 0x11);
    EXPECT_THROW(
        EncodePayload(oversized.data(), oversized.size()),
        std::runtime_error);
}

TEST(SqsPayload, RejectsGatewayErrorPrefix)
{
    EXPECT_NO_THROW(RejectGatewayErrorPayload({}));
    EXPECT_NO_THROW(RejectGatewayErrorPayload({0x01, 0x02}));

    std::vector<unsigned char> error{255, 'b', 'o', 'o', 'm'};
    try {
        RejectGatewayErrorPayload(error);
        FAIL() << "expected gateway error payload to throw";
    }
    catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "boom");
    }
}

TEST(SqsFifo, SetsGroupAndDedupOnlyForFifoQueues)
{
    Aws::SQS::Model::SendMessageRequest fifo;
    ConfigureFifoMessage(
        fifo,
        "https://sqs.us-east-1.amazonaws.com/1/orders.fifo",
        "",
        "dedup-1");
    EXPECT_EQ(std::string(fifo.GetMessageGroupId().c_str()), "graftcode");
    EXPECT_EQ(std::string(fifo.GetMessageDeduplicationId().c_str()), "dedup-1");

    Aws::SQS::Model::SendMessageRequest standard;
    ConfigureFifoMessage(
        standard,
        "https://sqs.us-east-1.amazonaws.com/1/orders",
        "group",
        "dedup-1");
    EXPECT_FALSE(standard.MessageGroupIdHasBeenSet());
    EXPECT_FALSE(standard.MessageDeduplicationIdHasBeenSet());
}

TEST(SqsTransport, FactoryCreatesAndDestroysTransport)
{
    auto* transport = CreateTransportChannel(nullptr, 0, kLocalConfig);
    ASSERT_NE(transport, nullptr);
    EXPECT_EQ(transport->Initialize(1, 2, 0), 0);

    EXPECT_THROW(transport->SendCommand(nullptr, -1), std::invalid_argument);
    EXPECT_THROW(transport->SendCommand(nullptr, 4), std::invalid_argument);

    unsigned char buffer[4]{};
    EXPECT_THROW(transport->ReadResponse(nullptr, 4), std::invalid_argument);
    EXPECT_THROW(transport->ReadResponse(buffer, 4), std::runtime_error);
    DestroyTransportChannel(transport);
}

TEST(SqsServer, FactoryConfigureAndStop)
{
    GraftcodeGateway::IServer* server = CreateServer();
    ASSERT_NE(server, nullptr);
    EXPECT_THROW(server->configure(kLocalConfig, nullptr), std::runtime_error);
    EXPECT_THROW(
        server->configure(R"({"region":"us-east-1"})", &EchoCallback),
        std::runtime_error);

    server->configure(kLocalConfig, &EchoCallback);
    server->stop();
    DestroyServer(server);
}

TEST(SqsServer, OneWayServerDoesNotRequireReplyQueue)
{
    SqsConfig config;
    config.requestQueueUrl = "https://example/requests";
    config.oneWay = true;
    EXPECT_NO_THROW(ValidateServerConfig(config));
    EXPECT_NO_THROW(ValidateClientConfig(config));
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

    auto runCall = [&](const std::string& callJson) {
        SqsClient client(ParseSqsConfigSource(callJson));
        return client.Call(kPayload.data(), kPayload.size());
    };

    std::vector<unsigned char> response;
    std::vector<unsigned char> emptyResponse;
    std::exception_ptr callError;
    try {
        response = runCall(json);
        emptyResponse = SqsClient(ParseSqsConfigSource(json))
            .Call(nullptr, 0);
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
    EXPECT_TRUE(emptyResponse.empty());

    g_oneWaySeen.store(false);
    const std::string oneWayJson =
        json.substr(0, json.size() - 1) + ",\"oneWay\":true}";
    SqsServer oneWayServer;
    oneWayServer.configure(oneWayJson.c_str(), &RecordOneWayCallback);
    std::thread oneWayThread([&oneWayServer]() {
        try {
            oneWayServer.start();
        }
        catch (...) {
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::vector<unsigned char> oneWayResult;
    std::exception_ptr oneWayError;
    try {
        oneWayResult =
            SqsClient(ParseSqsConfigSource(oneWayJson))
                .Call(kPayload.data(), kPayload.size());
    }
    catch (...) {
        oneWayError = std::current_exception();
    }

    for (int attempt = 0; attempt < 50 && !g_oneWaySeen.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    oneWayServer.stop();
    oneWayThread.join();
    if (oneWayError) {
        std::rethrow_exception(oneWayError);
    }
    EXPECT_TRUE(oneWayResult.empty());
    EXPECT_TRUE(g_oneWaySeen.load());
}
