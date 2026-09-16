#include "TransportKafka.h"
#include "KafkaServer.h"

#include <nlohmann/json.hpp>

#include <cstring>

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

int TransportKafka::SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) {
    lastResponse_ = client_->call(messageByteArray, static_cast<std::size_t>(messageByteArrayLen));
    return static_cast<int>(lastResponse_.size());
}

int TransportKafka::ReadResponse(byte* responseByteArray, int32_t responseByteArrayLen) {
    if (responseByteArrayLen < static_cast<int32_t>(lastResponse_.size())) {
        return -1;
    }
    if (!lastResponse_.empty()) {
        std::memcpy(responseByteArray, lastResponse_.data(), lastResponse_.size());
    }
    return 0;
}

KAFKA_PLUGIN_EXPORT Hypertube::Native::Interfaces::ITransport*
CreateTransportChannel(const char* ipAddress, const unsigned short port, const char* configSource) {
    return new TransportKafka(ipAddress, port, configSource);
}

KAFKA_PLUGIN_EXPORT void DestroyTransportChannel(Hypertube::Native::Interfaces::ITransport* transport) {
    delete transport;
}

KAFKA_PLUGIN_EXPORT GraftcodeGateway::IServer* CreateServer() {
    return new KafkaServer();
}

KAFKA_PLUGIN_EXPORT void DestroyServer(GraftcodeGateway::IServer* server) {
    delete server;
}
