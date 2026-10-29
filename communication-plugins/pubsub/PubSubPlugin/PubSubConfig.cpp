#include "PubSubConfig.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Graftcode::Plugins::PubSub
{
    namespace
    {
        using Json = nlohmann::json;

        std::string Trim(std::string value)
        {
            while (!value.empty() &&
                std::isspace(static_cast<unsigned char>(value.back()))) {
                value.pop_back();
            }
            std::size_t begin = 0;
            while (begin < value.size() &&
                std::isspace(static_cast<unsigned char>(value[begin]))) {
                ++begin;
            }
            value.erase(0, begin);
            return value;
        }

        Json ParseJsonSource(const std::string& source)
        {
            if (source.empty()) {
                throw std::runtime_error("Pub/Sub plugin: empty configuration");
            }

            const Json inlineJson = Json::parse(source, nullptr, false);
            if (!inlineJson.is_discarded()) {
                return inlineJson;
            }

            const std::filesystem::path path(source);
            std::ifstream file(path);
            if (!file.good()) {
                throw std::runtime_error(
                    "Pub/Sub plugin: configuration is neither valid JSON nor a readable file");
            }

            std::stringstream contents;
            contents << file.rdbuf();
            return Json::parse(contents.str());
        }

        bool HasSpecificPubSubKeys(const Json& node)
        {
            return node.contains("projectId") ||
                node.contains("requestTopic") ||
                node.contains("requestSubscription") ||
                node.contains("replySubscription") ||
                node.contains("emulatorHost") ||
                node.contains("credentialsFile") ||
                node.contains("accessToken") ||
                node.contains("ackDeadlineSeconds");
        }

        bool NameLooksLikePubSub(const Json& node)
        {
            const auto name = node.find("name");
            if (name == node.end() || !name->is_string()) {
                return false;
            }
            const auto pluginName = name->get<std::string>();
            return pluginName.find("PubSub") != std::string::npos ||
                pluginName.find("pubsub") != std::string::npos;
        }

        bool IsFlatPubSubAlias(const Json& node)
        {
            const bool hasAlias =
                node.contains("queue") ||
                node.contains("replyQueue") ||
                node.contains("requestTopic") ||
                node.contains("replyTopic");
            if (!hasAlias && !node.contains("projectId")) {
                return false;
            }
            if (HasSpecificPubSubKeys(node)) {
                return true;
            }
            return NameLooksLikePubSub(node);
        }

        const Json* FindPubSubNode(const Json& node)
        {
            if (node.is_object()) {
                const auto pubsub = node.find("pubsub");
                if (pubsub != node.end() && pubsub->is_object()) {
                    return &(*pubsub);
                }

                if (HasSpecificPubSubKeys(node) || IsFlatPubSubAlias(node)) {
                    return &node;
                }

                for (auto it = node.begin(); it != node.end(); ++it) {
                    if (const Json* found = FindPubSubNode(*it)) {
                        return found;
                    }
                }
            }
            else if (node.is_array()) {
                for (const auto& item : node) {
                    if (const Json* found = FindPubSubNode(item)) {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        void ReadString(
            const Json& node,
            const char* key,
            std::string& target)
        {
            const auto value = node.find(key);
            if (value != node.end() && value->is_string()) {
                target = value->get<std::string>();
            }
        }

        void ReadPositiveUint(
            const Json& node,
            const char* key,
            std::uint32_t& target)
        {
            const auto value = node.find(key);
            if (value == node.end()) {
                return;
            }
            if (!value->is_number_integer()) {
                throw std::runtime_error(
                    std::string("Pub/Sub plugin: '") + key + "' must be an integer");
            }

            const auto parsed = value->get<std::int64_t>();
            if (parsed <= 0 ||
                parsed > static_cast<std::int64_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {
                throw std::runtime_error(
                    std::string("Pub/Sub plugin: invalid value for '") + key + "'");
            }
            target = static_cast<std::uint32_t>(parsed);
        }

        void ReadBool(const Json& node, const char* key, bool& target, bool* specified)
        {
            const auto value = node.find(key);
            if (value == node.end()) {
                return;
            }
            if (!value->is_boolean()) {
                throw std::runtime_error(
                    std::string("Pub/Sub plugin: '") + key + "' must be a boolean");
            }
            target = value->get<bool>();
            if (specified != nullptr) {
                *specified = true;
            }
        }

        void DefaultSubscription(std::string& subscription, const std::string& topic)
        {
            if (!subscription.empty() || topic.empty()) {
                return;
            }
            subscription = topic + "-sub";
        }

        void ValidateCommon(const PubSubConfig& config, const char* role)
        {
            const std::string prefix = std::string("Pub/Sub ") + role + ": ";
            if (config.projectId.empty()) {
                throw std::runtime_error(prefix + "missing required field 'projectId'");
            }
            if (!IsValidProjectId(config.projectId)) {
                throw std::runtime_error(
                    prefix + "'projectId' must be 1-128 letters, digits, '-' or '_'");
            }
            if (config.ackDeadlineSeconds < 10 || config.ackDeadlineSeconds > 600) {
                throw std::runtime_error(
                    prefix + "'ackDeadlineSeconds' must be between 10 and 600");
            }
            if (config.maxMessages < 1 || config.maxMessages > 100) {
                throw std::runtime_error(
                    prefix + "'maxMessages' must be between 1 and 100");
            }
            if (config.rpcTimeoutMs == 0 || config.rpcTimeoutMs > 3600000) {
                throw std::runtime_error(
                    prefix + "'rpcTimeoutMs' must be between 1 and 3600000");
            }
            if (!config.emulatorHost.empty() &&
                (config.emulatorHost.find('/') != std::string::npos ||
                    config.emulatorHost.find(' ') != std::string::npos)) {
                throw std::runtime_error(
                    prefix + "'emulatorHost' must be host or host:port");
            }
        }

        void ValidateResource(
            const std::string& value,
            const char* role,
            const char* field,
            bool required)
        {
            if (value.empty()) {
                if (required) {
                    throw std::runtime_error(
                        std::string("Pub/Sub ") + role + ": missing required field '" +
                        field + "'");
                }
                return;
            }
            if (!IsValidResourceId(value)) {
                throw std::runtime_error(
                    std::string("Pub/Sub ") + role + ": '" + field +
                    "' must be 3-255 characters, start with a letter, and contain only letters, digits, '-', '_' or '.'");
            }
        }
    }

    bool IsValidProjectId(const std::string& value)
    {
        if (value.empty() || value.size() > 128) {
            return false;
        }
        for (const char character : value) {
            const auto byte = static_cast<unsigned char>(character);
            if (std::isalnum(byte) || character == '-' || character == '_') {
                continue;
            }
            return false;
        }
        return true;
    }

    bool IsValidResourceId(const std::string& value)
    {
        if (value.size() < 3 || value.size() > 255) {
            return false;
        }
        if (!std::isalpha(static_cast<unsigned char>(value.front()))) {
            return false;
        }
        for (const char character : value) {
            const auto byte = static_cast<unsigned char>(character);
            if (std::isalnum(byte) ||
                character == '-' ||
                character == '_' ||
                character == '.') {
                continue;
            }
            return false;
        }
        return true;
    }

    std::string EndpointBase(const PubSubConfig& config)
    {
        if (config.emulatorHost.empty()) {
            return "https://pubsub.googleapis.com";
        }
        return (config.emulatorUseHttps ? "https://" : "http://") + config.emulatorHost;
    }

    void NormalizeEmulatorHost(PubSubConfig& config)
    {
        std::string value = Trim(config.emulatorHost);
        if (value.empty()) {
            config.emulatorHost.clear();
            return;
        }

        config.emulatorUseHttps = false;
        constexpr const char* http = "http://";
        constexpr const char* https = "https://";
        if (value.rfind(https, 0) == 0) {
            config.emulatorUseHttps = true;
            value.erase(0, 8);
        }
        else if (value.rfind(http, 0) == 0) {
            value.erase(0, 7);
        }
        while (!value.empty() && value.back() == '/') {
            value.pop_back();
        }
        if (value.empty() ||
            value.find('/') != std::string::npos ||
            value.find(' ') != std::string::npos) {
            throw std::runtime_error(
                "Pub/Sub plugin: 'emulatorHost' must be host or host:port");
        }
        config.emulatorHost = std::move(value);
    }

    PubSubConfig ParsePubSubConfigSource(const std::string& configSource)
    {
        const Json root = ParseJsonSource(configSource);
        const Json* node = FindPubSubNode(root);
        if (node == nullptr || !node->is_object()) {
            throw std::runtime_error(
                "Pub/Sub plugin: configuration does not contain Pub/Sub settings");
        }

        PubSubConfig config;
        ReadString(*node, "name", config.name);
        ReadString(*node, "projectId", config.projectId);
        ReadString(*node, "requestTopic", config.requestTopic);
        ReadString(*node, "requestSubscription", config.requestSubscription);
        ReadString(*node, "replyTopic", config.replyTopic);
        ReadString(*node, "replySubscription", config.replySubscription);
        ReadString(*node, "emulatorHost", config.emulatorHost);
        ReadString(*node, "credentialsFile", config.credentialsFile);
        ReadString(*node, "accessToken", config.accessToken);

        if (config.requestTopic.empty()) {
            ReadString(*node, "queue", config.requestTopic);
        }
        if (config.replyTopic.empty()) {
            ReadString(*node, "replyQueue", config.replyTopic);
        }
        if (config.requestSubscription.empty()) {
            ReadString(*node, "subscription", config.requestSubscription);
        }

        ReadPositiveUint(*node, "rpcTimeoutMs", config.rpcTimeoutMs);
        ReadPositiveUint(*node, "ackDeadlineSeconds", config.ackDeadlineSeconds);
        ReadPositiveUint(*node, "maxMessages", config.maxMessages);
        ReadBool(*node, "oneWay", config.oneWay, nullptr);
        ReadBool(*node, "autoCreate", config.autoCreate, &config.autoCreateSpecified);
        ReadBool(*node, "verifySsl", config.verifySsl, nullptr);

        if (config.ackDeadlineSeconds < 10 || config.ackDeadlineSeconds > 600) {
            throw std::runtime_error(
                "Pub/Sub plugin: 'ackDeadlineSeconds' must be between 10 and 600");
        }
        if (config.maxMessages < 1 || config.maxMessages > 100) {
            throw std::runtime_error(
                "Pub/Sub plugin: 'maxMessages' must be between 1 and 100");
        }
        if (config.rpcTimeoutMs > 3600000) {
            throw std::runtime_error(
                "Pub/Sub plugin: 'rpcTimeoutMs' cannot exceed 3600000");
        }

        NormalizeEmulatorHost(config);
        DefaultSubscription(config.requestSubscription, config.requestTopic);
        DefaultSubscription(config.replySubscription, config.replyTopic);
        return config;
    }

    void ApplyRuntimeEnvironment(PubSubConfig& config)
    {
        if (config.emulatorHost.empty()) {
            if (const char* host = std::getenv("PUBSUB_EMULATOR_HOST")) {
                config.emulatorHost = host;
                NormalizeEmulatorHost(config);
            }
        }
        if (config.accessToken.empty()) {
            if (const char* token = std::getenv("PUBSUB_ACCESS_TOKEN")) {
                config.accessToken = token;
            }
        }
        if (config.credentialsFile.empty()) {
            if (const char* path = std::getenv("GOOGLE_APPLICATION_CREDENTIALS")) {
                config.credentialsFile = path;
            }
        }
        if (!config.autoCreateSpecified) {
            config.autoCreate = !config.emulatorHost.empty();
        }
    }

    void ValidateClientConfig(const PubSubConfig& config)
    {
        ValidateCommon(config, "client");
        ValidateResource(config.requestTopic, "client", "requestTopic", true);
        if (!config.oneWay) {
            ValidateResource(config.replyTopic, "client", "replyTopic", true);
            ValidateResource(
                config.replySubscription,
                "client",
                "replySubscription",
                true);
        }
    }

    void ValidateServerConfig(const PubSubConfig& config)
    {
        ValidateCommon(config, "server");
        ValidateResource(config.requestTopic, "server", "requestTopic", true);
        ValidateResource(
            config.requestSubscription,
            "server",
            "requestSubscription",
            true);
    }
}
