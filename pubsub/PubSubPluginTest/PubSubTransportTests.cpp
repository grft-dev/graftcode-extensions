#include <gtest/gtest.h>

#include "IServer.h"
#include "ITransport.h"
#include "PubSubClient.h"
#include "PubSubConfig.h"
#include "PubSubServer.h"
#include "PubSubUtil.h"
#include "TransportPubSub.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
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

using Graftcode::Plugins::PubSub::ApplyRuntimeEnvironment;
using Graftcode::Plugins::PubSub::Base64Decode;
using Graftcode::Plugins::PubSub::Base64Encode;
using Graftcode::Plugins::PubSub::BuildServiceAccountJwt;
using Graftcode::Plugins::PubSub::EndpointBase;
using Graftcode::Plugins::PubSub::IsValidResourceId;
using Graftcode::Plugins::PubSub::MaxPayloadBytes;
using Graftcode::Plugins::PubSub::ParsePubSubConfigSource;
using Graftcode::Plugins::PubSub::PubSubClient;
using Graftcode::Plugins::PubSub::PubSubConfig;
using Graftcode::Plugins::PubSub::PubSubServer;
using Graftcode::Plugins::PubSub::RejectGatewayErrorPayload;
using Graftcode::Plugins::PubSub::ValidateClientConfig;
using Graftcode::Plugins::PubSub::ValidateServerConfig;

namespace
{
    const char* kLocalConfig = R"({
        "name": "PubSubPlugin",
        "projectId": "graftcode-local",
        "emulatorHost": "http://127.0.0.1:8085/",
        "requestTopic": "graft-requests",
        "replyTopic": "graft-replies",
        "rpcTimeoutMs": 5000,
        "ackDeadlineSeconds": 30,
        "autoCreate": false,
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
        return value != nullptr && value[0] != '\0' ? value : fallback;
    }

    void SetEnv(const char* name, const char* value)
    {
#if defined(_WIN32)
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    void UnsetEnv(const char* name)
    {
#if defined(_WIN32)
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    }

    struct EnvGuard
    {
        EnvGuard(const char* name, const char* value)
            : name_(name)
        {
            const char* existing = std::getenv(name);
            if (existing != nullptr) {
                hadPrevious_ = true;
                previous_ = existing;
            }
            SetEnv(name, value);
        }

        ~EnvGuard()
        {
            if (hadPrevious_) {
                SetEnv(name_, previous_.c_str());
            }
            else {
                UnsetEnv(name_);
            }
        }

        const char* name_;
        bool hadPrevious_{ false };
        std::string previous_;
    };

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

    std::string Base64UrlToStandard(std::string value)
    {
        for (char& character : value) {
            if (character == '-') {
                character = '+';
            }
            else if (character == '_') {
                character = '/';
            }
        }
        while (value.size() % 4 != 0) {
            value.push_back('=');
        }
        return value;
    }

    struct GeneratedKey
    {
        std::string pem;
        EVP_PKEY* key{ nullptr };

        ~GeneratedKey() { EVP_PKEY_free(key); }
    };

    GeneratedKey GenerateRsaKey()
    {
        EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (context == nullptr ||
            EVP_PKEY_keygen_init(context) != 1 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) != 1) {
            EVP_PKEY_CTX_free(context);
            throw std::runtime_error("failed to initialize RSA key generation");
        }
        GeneratedKey generated;
        if (EVP_PKEY_keygen(context, &generated.key) != 1 ||
            generated.key == nullptr) {
            EVP_PKEY_CTX_free(context);
            throw std::runtime_error("failed to generate RSA key");
        }
        EVP_PKEY_CTX_free(context);

        BIO* bio = BIO_new(BIO_s_mem());
        if (bio == nullptr ||
            PEM_write_bio_PrivateKey(
                bio,
                generated.key,
                nullptr,
                nullptr,
                0,
                nullptr,
                nullptr) != 1) {
            BIO_free(bio);
            throw std::runtime_error("failed to encode RSA key");
        }
        char* data = nullptr;
        const long length = BIO_get_mem_data(bio, &data);
        generated.pem.assign(data, static_cast<std::size_t>(length));
        BIO_free(bio);
        return generated;
    }
}

TEST(PubSubConfig, ParsesTopicsAndEmulatorSettings)
{
    const PubSubConfig config = ParsePubSubConfigSource(kLocalConfig);

    EXPECT_EQ(config.projectId, "graftcode-local");
    EXPECT_EQ(config.emulatorHost, "127.0.0.1:8085");
    EXPECT_FALSE(config.emulatorUseHttps);
    EXPECT_EQ(config.requestTopic, "graft-requests");
    EXPECT_EQ(config.requestSubscription, "graft-requests-sub");
    EXPECT_EQ(config.replyTopic, "graft-replies");
    EXPECT_EQ(config.replySubscription, "graft-replies-sub");
    EXPECT_EQ(config.rpcTimeoutMs, 5000u);
    EXPECT_EQ(config.ackDeadlineSeconds, 30u);
    EXPECT_FALSE(config.autoCreate);
    EXPECT_TRUE(config.autoCreateSpecified);
    EXPECT_FALSE(config.verifySsl);
    EXPECT_EQ(EndpointBase(config), "http://127.0.0.1:8085");
    EXPECT_NO_THROW(ValidateClientConfig(config));
    EXPECT_NO_THROW(ValidateServerConfig(config));
}

TEST(PubSubConfig, FindsNestedPubSubNode)
{
    const PubSubConfig config = ParsePubSubConfigSource(R"({
        "configurations": {
            "package": {
                "plugin": {
                    "pubsub": {
                        "projectId": "demo-project",
                        "requestTopic": "graft-requests",
                        "replyTopic": "graft-replies",
                        "emulatorHost": "https://pubsub.example:443"
                    }
                }
            }
        }
    })");

    EXPECT_EQ(config.projectId, "demo-project");
    EXPECT_EQ(config.requestTopic, "graft-requests");
    EXPECT_TRUE(config.emulatorUseHttps);
    EXPECT_EQ(config.emulatorHost, "pubsub.example:443");
    EXPECT_EQ(EndpointBase(config), "https://pubsub.example:443");
}

TEST(PubSubConfig, RequiresReplyTopicForRpc)
{
    PubSubConfig config;
    config.projectId = "graftcode-local";
    config.requestTopic = "graft-requests";
    config.requestSubscription = "graft-requests-sub";
    EXPECT_THROW(ValidateClientConfig(config), std::runtime_error);

    config.oneWay = true;
    EXPECT_NO_THROW(ValidateClientConfig(config));
    EXPECT_NO_THROW(ValidateServerConfig(config));
}

TEST(PubSubConfig, AcceptsFlatQueueAlias)
{
    const PubSubConfig config = ParsePubSubConfigSource(R"({
        "name": "PubSubPlugin",
        "projectId": "graftcode-local",
        "queue": "graft-requests",
        "replyQueue": "graft-replies"
    })");

    EXPECT_EQ(config.requestTopic, "graft-requests");
    EXPECT_EQ(config.replyTopic, "graft-replies");
    EXPECT_EQ(config.requestSubscription, "graft-requests-sub");
}

TEST(PubSubConfig, ReadsNestedAliasAheadOfForeignQueue)
{
    const PubSubConfig config = ParsePubSubConfigSource(R"({
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
                    "name": "PubSubPlugin",
                    "projectId": "graftcode-local",
                    "queue": "graft-requests",
                    "replyQueue": "graft-replies"
                }
            }
        }
    })");

    EXPECT_EQ(config.projectId, "graftcode-local");
    EXPECT_EQ(config.requestTopic, "graft-requests");
    EXPECT_EQ(config.replyTopic, "graft-replies");
}

TEST(PubSubConfig, RejectsForeignQueueConfig)
{
    EXPECT_THROW(
        ParsePubSubConfigSource(R"({
            "name": "RabbitmqPlugin",
            "queue": "myqueue",
            "host": "localhost"
        })"),
        std::runtime_error);
}

TEST(PubSubConfig, ReadsConfigurationFile)
{
    const auto path =
        std::filesystem::temp_directory_path() / "graftcode-pubsub-config.json";
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.good());
        file << R"({
            "projectId": "from-file",
            "requestTopic": "graft-requests",
            "replyTopic": "graft-replies",
            "subscription": "custom-requests-sub",
            "oneWay": false
        })";
    }

    const PubSubConfig config = ParsePubSubConfigSource(path.string());
    EXPECT_EQ(config.projectId, "from-file");
    EXPECT_EQ(config.requestSubscription, "custom-requests-sub");
    EXPECT_EQ(config.replySubscription, "graft-replies-sub");
    std::filesystem::remove(path);
}

TEST(PubSubConfig, RejectsInvalidValues)
{
    EXPECT_THROW(
        ParsePubSubConfigSource(
            R"({"ackDeadlineSeconds": 9, "projectId": "p", "requestTopic": "graft-requests"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParsePubSubConfigSource(
            R"({"ackDeadlineSeconds": 601, "projectId": "p", "requestTopic": "graft-requests"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParsePubSubConfigSource(
            R"({"maxMessages": 0, "projectId": "p", "requestTopic": "graft-requests"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParsePubSubConfigSource(
            R"({"oneWay": "yes", "projectId": "p", "requestTopic": "graft-requests"})"),
        std::runtime_error);
    EXPECT_THROW(
        ParsePubSubConfigSource(
            R"({"emulatorHost": "http:///", "projectId": "p", "requestTopic": "graft-requests"})"),
        std::runtime_error);
    EXPECT_THROW(ParsePubSubConfigSource(""), std::runtime_error);
    EXPECT_THROW(ParsePubSubConfigSource("not-json-or-a-file"), std::runtime_error);
    EXPECT_FALSE(IsValidResourceId("ab"));
    EXPECT_FALSE(IsValidResourceId("1graft-requests"));
    EXPECT_TRUE(IsValidResourceId("graft-requests"));
}

TEST(PubSubConfig, EmulatorEnvEnablesAutoCreate)
{
    EnvGuard host("PUBSUB_EMULATOR_HOST", "127.0.0.1:8085");
    EnvGuard token("PUBSUB_ACCESS_TOKEN", "local-token");
    PubSubConfig config = ParsePubSubConfigSource(R"({
        "projectId": "graftcode-local",
        "requestTopic": "graft-requests",
        "replyTopic": "graft-replies"
    })");
    EXPECT_TRUE(config.emulatorHost.empty());
    EXPECT_FALSE(config.autoCreate);

    ApplyRuntimeEnvironment(config);
    EXPECT_EQ(config.emulatorHost, "127.0.0.1:8085");
    EXPECT_TRUE(config.autoCreate);
    EXPECT_EQ(config.accessToken, "local-token");
}

TEST(PubSubPayload, PreservesArbitraryBinaryData)
{
    const std::string encoded = Base64Encode(kPayload.data(), kPayload.size());
    EXPECT_EQ(Base64Decode(encoded), kPayload);
}

TEST(PubSubPayload, SupportsEmptyPayloadAndMissingPadding)
{
    EXPECT_TRUE(Base64Encode(nullptr, 0).empty());
    EXPECT_TRUE(Base64Decode("").empty());

    const std::string encoded = Base64Encode(kPayload.data(), kPayload.size());
    std::string unpadded = encoded;
    while (!unpadded.empty() && unpadded.back() == '=') {
        unpadded.pop_back();
    }
    EXPECT_EQ(Base64Decode(unpadded), kPayload);
    EXPECT_THROW(Base64Decode("****"), std::runtime_error);
}

TEST(PubSubPayload, RejectsGatewayErrorPrefix)
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

TEST(PubSubPayload, RejectsOversizedPayloadBeforeNetwork)
{
    PubSubConfig config = ParsePubSubConfigSource(kLocalConfig);
    PubSubClient client(config);
    EXPECT_THROW(
        client.Call(nullptr, MaxPayloadBytes + 1),
        std::runtime_error);
    EXPECT_THROW(client.Call(nullptr, 4), std::invalid_argument);
}

TEST(PubSubAuth, ServiceAccountJwtIsRs256)
{
    const GeneratedKey generated = GenerateRsaKey();
    const std::string jwt = BuildServiceAccountJwt(
        "caller@graftcode-local.iam.gserviceaccount.com",
        generated.pem,
        "https://oauth2.googleapis.com/token",
        1'700'000'000);

    const auto firstDot = jwt.find('.');
    const auto secondDot = jwt.find('.', firstDot + 1);
    ASSERT_NE(firstDot, std::string::npos);
    ASSERT_NE(secondDot, std::string::npos);

    const std::string signingInput = jwt.substr(0, secondDot);
    const auto header = Base64Decode(Base64UrlToStandard(jwt.substr(0, firstDot)));
    const auto payload = Base64Decode(
        Base64UrlToStandard(jwt.substr(firstDot + 1, secondDot - firstDot - 1)));
    const auto signature = Base64Decode(Base64UrlToStandard(jwt.substr(secondDot + 1)));
    const std::string headerText(header.begin(), header.end());
    const std::string payloadText(payload.begin(), payload.end());
    EXPECT_NE(headerText.find("RS256"), std::string::npos);
    EXPECT_NE(payloadText.find("caller@graftcode-local.iam.gserviceaccount.com"), std::string::npos);
    EXPECT_NE(payloadText.find("https://www.googleapis.com/auth/pubsub"), std::string::npos);

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(
        EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, generated.key),
        1);
    ASSERT_EQ(
        EVP_DigestVerifyUpdate(context, signingInput.data(), signingInput.size()),
        1);
    EXPECT_EQ(
        EVP_DigestVerifyFinal(
            context,
            signature.data(),
            signature.size()),
        1);
    EVP_MD_CTX_free(context);

    EXPECT_THROW(
        BuildServiceAccountJwt("caller@example.com", "not-a-key", "", 1),
        std::runtime_error);
}

TEST(PubSubTransport, FactoryCreatesAndDestroysTransport)
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

TEST(PubSubServer, FactoryConfigureAndStop)
{
    GraftcodeGateway::IServer* server = CreateServer();
    ASSERT_NE(server, nullptr);
    EXPECT_THROW(server->configure(kLocalConfig, nullptr), std::runtime_error);
    EXPECT_THROW(
        server->configure(R"({"projectId":"graftcode-local"})", &EchoCallback),
        std::runtime_error);

    server->configure(kLocalConfig, &EchoCallback);
    server->stop();
    DestroyServer(server);
}

TEST(PubSubServer, OneWayServerDoesNotRequireReplyTopic)
{
    const char* config = R"({
        "projectId": "graftcode-local",
        "requestTopic": "graft-requests",
        "oneWay": true,
        "autoCreate": false
    })";
    PubSubConfig parsed = ParsePubSubConfigSource(config);
    EXPECT_NO_THROW(ValidateServerConfig(parsed));
    EXPECT_NO_THROW(ValidateClientConfig(parsed));

    PubSubServer server;
    server.configure(config, &EchoCallback);
    server.stop();
}

TEST(PubSubLive, RpcRoundTrip)
{
    const char* emulator = std::getenv("PUBSUB_EMULATOR_HOST");
    const char* live = std::getenv("PUBSUB_LIVE");
    if ((emulator == nullptr || emulator[0] == '\0') &&
        (live == nullptr || live[0] == '\0')) {
        GTEST_SKIP()
            << "PUBSUB_EMULATOR_HOST or PUBSUB_LIVE is not set";
    }

    const std::string host = EnvOr("PUBSUB_EMULATOR_HOST", "localhost:8085");
    const std::string project = EnvOr("PUBSUB_PROJECT_ID", "graftcode-local");
    const std::string suffix =
        Graftcode::Plugins::PubSub::NewCorrelationId().substr(0, 12);
    const std::string requestTopic = "graftreq" + suffix;
    const std::string replyTopic = "graftrep" + suffix;

    const std::string json =
        std::string("{\"projectId\":\"") + EscapeJson(project) +
        "\",\"emulatorHost\":\"" + EscapeJson(host) +
        "\",\"requestTopic\":\"" + requestTopic +
        "\",\"replyTopic\":\"" + replyTopic +
        "\",\"autoCreate\":true,\"verifySsl\":false,\"rpcTimeoutMs\":20000,"
        "\"ackDeadlineSeconds\":30}";

    PubSubServer server;
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
    std::vector<unsigned char> emptyResponse;
    std::exception_ptr callError;
    try {
        PubSubClient client(ParsePubSubConfigSource(json));
        response = client.Call(kPayload.data(), kPayload.size());
        emptyResponse = PubSubClient(ParsePubSubConfigSource(json)).Call(nullptr, 0);
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
    PubSubServer oneWayServer;
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
        oneWayResult = PubSubClient(ParsePubSubConfigSource(oneWayJson))
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
