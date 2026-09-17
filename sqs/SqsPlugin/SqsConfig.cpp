#include "SqsConfig.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace Graftcode::Plugins::Sqs
{
    namespace
    {
        using Json = nlohmann::json;

        Json ParseJsonSource(const std::string& source)
        {
            if (source.empty()) {
                throw std::runtime_error("SQS plugin: empty configuration");
            }

            const Json inlineJson = Json::parse(source, nullptr, false);
            if (!inlineJson.is_discarded()) {
                return inlineJson;
            }

            const std::filesystem::path path(source);
            std::ifstream file(path);
            if (!file.good()) {
                throw std::runtime_error(
                    "SQS plugin: configuration is neither valid JSON nor a readable file");
            }

            std::stringstream contents;
            contents << file.rdbuf();
            return Json::parse(contents.str());
        }

        const Json* FindSqsNode(const Json& node)
        {
            if (node.is_object()) {
                const auto sqs = node.find("sqs");
                if (sqs != node.end() && sqs->is_object()) {
                    return &(*sqs);
                }

                const bool hasSqsKeys =
                    node.contains("requestQueueUrl") ||
                    node.contains("replyQueueUrl") ||
                    node.contains("queueUrl") ||
                    node.contains("queue") ||
                    node.contains("replyQueue") ||
                    node.contains("endpointOverride");
                if (hasSqsKeys) {
                    return &node;
                }

                for (auto it = node.begin(); it != node.end(); ++it) {
                    if (const Json* found = FindSqsNode(*it)) {
                        return found;
                    }
                }
            }
            else if (node.is_array()) {
                for (const auto& item : node) {
                    if (const Json* found = FindSqsNode(item)) {
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
                    std::string("SQS plugin: '") + key + "' must be an integer");
            }

            const auto parsed = value->get<std::int64_t>();
            if (parsed <= 0 ||
                parsed > static_cast<std::int64_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {
                throw std::runtime_error(
                    std::string("SQS plugin: invalid value for '") + key + "'");
            }
            target = static_cast<std::uint32_t>(parsed);
        }

        void ReadBool(const Json& node, const char* key, bool& target)
        {
            const auto value = node.find(key);
            if (value == node.end()) {
                return;
            }
            if (!value->is_boolean()) {
                throw std::runtime_error(
                    std::string("SQS plugin: '") + key + "' must be a boolean");
            }
            target = value->get<bool>();
        }
    }

    SqsConfig ParseSqsConfigSource(const std::string& configSource)
    {
        const Json root = ParseJsonSource(configSource);
        const Json* node = FindSqsNode(root);
        if (node == nullptr || !node->is_object()) {
            throw std::runtime_error("SQS plugin: configuration must be a JSON object");
        }

        SqsConfig config;
        ReadString(*node, "name", config.name);
        ReadString(*node, "region", config.region);
        ReadString(*node, "endpointOverride", config.endpointOverride);
        ReadString(*node, "accessKeyId", config.accessKeyId);
        ReadString(*node, "secretAccessKey", config.secretAccessKey);
        ReadString(*node, "sessionToken", config.sessionToken);
        ReadString(*node, "requestQueueUrl", config.requestQueueUrl);
        ReadString(*node, "replyQueueUrl", config.replyQueueUrl);
        ReadString(*node, "messageGroupId", config.messageGroupId);

        if (config.requestQueueUrl.empty()) {
            ReadString(*node, "queueUrl", config.requestQueueUrl);
        }
        if (config.requestQueueUrl.empty()) {
            ReadString(*node, "queue", config.requestQueueUrl);
        }
        if (config.replyQueueUrl.empty()) {
            ReadString(*node, "replyQueue", config.replyQueueUrl);
        }

        ReadPositiveUint(*node, "rpcTimeoutMs", config.rpcTimeoutMs);
        ReadPositiveUint(*node, "waitTimeSeconds", config.waitTimeSeconds);
        ReadPositiveUint(
            *node,
            "visibilityTimeoutSeconds",
            config.visibilityTimeoutSeconds);
        ReadBool(*node, "oneWay", config.oneWay);
        ReadBool(*node, "verifySsl", config.verifySsl);

        if (config.waitTimeSeconds > 20) {
            throw std::runtime_error(
                "SQS plugin: 'waitTimeSeconds' cannot exceed 20");
        }
        if (config.visibilityTimeoutSeconds > 43200) {
            throw std::runtime_error(
                "SQS plugin: 'visibilityTimeoutSeconds' cannot exceed 43200");
        }
        if (config.region.empty()) {
            throw std::runtime_error("SQS plugin: 'region' cannot be empty");
        }

        const bool hasAccessKey = !config.accessKeyId.empty();
        const bool hasSecretKey = !config.secretAccessKey.empty();
        if (hasAccessKey != hasSecretKey) {
            throw std::runtime_error(
                "SQS plugin: 'accessKeyId' and 'secretAccessKey' must be provided together");
        }

        return config;
    }

    void ValidateClientConfig(const SqsConfig& config)
    {
        if (config.requestQueueUrl.empty()) {
            throw std::runtime_error(
                "SQS client: missing required field 'requestQueueUrl'");
        }
        if (!config.oneWay && config.replyQueueUrl.empty()) {
            throw std::runtime_error(
                "SQS client: missing required field 'replyQueueUrl'");
        }
    }

    void ValidateServerConfig(const SqsConfig& config)
    {
        if (config.requestQueueUrl.empty()) {
            throw std::runtime_error(
                "SQS server: missing required field 'requestQueueUrl'");
        }
    }

    bool IsFifoQueueUrl(const std::string& queueUrl)
    {
        std::string normalized = queueUrl;
        if (const auto query = normalized.find('?');
            query != std::string::npos) {
            normalized.erase(query);
        }
        while (!normalized.empty() && normalized.back() == '/') {
            normalized.pop_back();
        }

        constexpr const char* suffix = ".fifo";
        return normalized.size() >= 5 &&
            normalized.compare(normalized.size() - 5, 5, suffix) == 0;
    }
}
