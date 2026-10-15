#include "TransportKafka.h"
#include "KafkaServer.h"
#include "TransportPluginExports.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define KAFKA_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define KAFKA_PLUGIN_EXPORT extern "C"
#endif

static KafkaClient::Config parseClientConfig(const char* configJson) {
    KafkaClient::Config c;
    if (!configJson) {
        return c;
    }
    auto j = nlohmann::json::parse(configJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return c;
    }
    if (j.contains("brokers") && j["brokers"].is_string()) {
        c.brokers = j["brokers"].get<std::string>();
    } else if (j.contains("host") && j["host"].is_string()) {
        c.brokers = j["host"].get<std::string>();
    }
    if (j.contains("requestTopic") && j["requestTopic"].is_string()) {
        c.requestTopic = j["requestTopic"].get<std::string>();
    } else if (j.contains("queue") && j["queue"].is_string()) {
        c.requestTopic = j["queue"].get<std::string>();
    }
    if (j.contains("replyTopic") && j["replyTopic"].is_string()) {
        c.replyTopic = j["replyTopic"].get<std::string>();
    } else if (j.contains("replyQueue") && j["replyQueue"].is_string()) {
        c.replyTopic = j["replyQueue"].get<std::string>();
    }
    if (j.contains("groupId") && j["groupId"].is_string()) {
        c.groupId = j["groupId"].get<std::string>();
    }
    if (j.contains("rpcTimeoutMs") && j["rpcTimeoutMs"].is_number_integer()) {
        c.rpcTimeoutMs = j["rpcTimeoutMs"].get<int>();
    }
    if (j.contains("securityProtocol") && j["securityProtocol"].is_string()) {
        c.securityProtocol = j["securityProtocol"].get<std::string>();
    }
    if (j.contains("saslMechanism") && j["saslMechanism"].is_string()) {
        c.saslMechanism = j["saslMechanism"].get<std::string>();
    }
    if (j.contains("saslUsername") && j["saslUsername"].is_string()) {
        c.saslUsername = j["saslUsername"].get<std::string>();
    }
    if (j.contains("saslPassword") && j["saslPassword"].is_string()) {
        c.saslPassword = j["saslPassword"].get<std::string>();
    }
    if (j.contains("sslCaLocation") && j["sslCaLocation"].is_string()) {
        c.sslCaLocation = j["sslCaLocation"].get<std::string>();
    }
    return c;
}

TransportKafka::TransportKafka(const char*, unsigned short, const char* configJson)
    : client_(std::make_unique<KafkaClient>(parseClientConfig(configJson))) {}

int TransportKafka::Initialize(byte, byte, byte) { return 0; }

std::vector<byte> TransportKafka::SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) {
    std::vector<byte> response = client_->call(messageByteArray, static_cast<std::size_t>(messageByteArrayLen));
    if (!response.empty() && response.front() == static_cast<byte>(255)) {
        throw std::runtime_error(std::string(response.begin() + 1, response.end()));
    }
    return response;
}

KAFKA_PLUGIN_EXPORT TransportPluginChannel* CreateTransport(const char* ipAddress, unsigned short port, const char* configSource, char** errorMessage) {
    return Graftcode::Plugins::TransportAbi::CreateTransport([&] { return new TransportKafka(ipAddress, port, configSource); }, errorMessage);
}

KAFKA_PLUGIN_EXPORT void DestroyTransport(TransportPluginChannel* channel) {
    Graftcode::Plugins::TransportAbi::DestroyTransport(channel);
}

KAFKA_PLUGIN_EXPORT int32_t TransportSendCommand(TransportPluginChannel* channel, const unsigned char* message, int32_t messageLen, unsigned char** response, int32_t* responseLen) {
    return Graftcode::Plugins::TransportAbi::SendCommand(channel, message, messageLen, response, responseLen);
}

KAFKA_PLUGIN_EXPORT void TransportFreeBuffer(void* buffer) {
    Graftcode::Plugins::TransportAbi::FreeBuffer(buffer);
}

KAFKA_PLUGIN_EXPORT GraftcodeGateway::IServer* CreateServer() {
    return new KafkaServer();
}

KAFKA_PLUGIN_EXPORT void DestroyServer(GraftcodeGateway::IServer* server) {
    delete server;
}
