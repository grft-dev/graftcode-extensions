#include "SqsUtil.h"

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/Array.h>
#include <aws/core/utils/HashingUtils.h>
#include <aws/sqs/SQSClient.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace Graftcode::Plugins::Sqs
{
    namespace
    {
        class AwsSdkLifetime
        {
        public:
            AwsSdkLifetime()
            {
                lifecycleThread_ = std::thread([this]() {
                    Aws::SDKOptions options;
                    Aws::InitAPI(options);
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        initialized_ = true;
                    }
                    condition_.notify_all();

                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this]() { return shutdown_; });
                    lock.unlock();
                    Aws::ShutdownAPI(options);
                });

                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() { return initialized_; });
            }

            ~AwsSdkLifetime()
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    shutdown_ = true;
                }
                condition_.notify_all();
                lifecycleThread_.join();
            }

        private:
            std::mutex mutex_;
            std::condition_variable condition_;
            bool initialized_{ false };
            bool shutdown_{ false };
            std::thread lifecycleThread_;
        };

        void EnsureAwsSdkInitialized()
        {
            static AwsSdkLifetime lifetime;
            (void)lifetime;
        }
    }

    std::unique_ptr<Aws::SQS::SQSClient> CreateAwsSqsClient(
        const SqsConfig& config)
    {
        EnsureAwsSdkInitialized();

        Aws::Client::ClientConfiguration clientConfig;
        clientConfig.region = config.region.c_str();
        clientConfig.verifySSL = config.verifySsl;
        clientConfig.connectTimeoutMs = 5000;
        clientConfig.requestTimeoutMs = static_cast<long>(
            (std::max)(
                std::uint32_t{ 30000 },
                (config.waitTimeSeconds + 5) * 1000));
        if (!config.endpointOverride.empty()) {
            clientConfig.endpointOverride = config.endpointOverride.c_str();
        }

        if (!config.accessKeyId.empty()) {
            const Aws::Auth::AWSCredentials credentials(
                config.accessKeyId.c_str(),
                config.secretAccessKey.c_str(),
                config.sessionToken.c_str());
            return std::make_unique<Aws::SQS::SQSClient>(
                credentials,
                clientConfig);
        }

        return std::make_unique<Aws::SQS::SQSClient>(clientConfig);
    }

    Aws::String EncodePayload(const unsigned char* data, std::size_t size)
    {
        if (size > 0 && data == nullptr) {
            throw std::invalid_argument(
                "SQS plugin: null payload with non-zero length");
        }

        const Aws::Utils::ByteBuffer bytes(
            data != nullptr ? data : reinterpret_cast<const unsigned char*>(""),
            size);
        Aws::String encoded = "graftcode-base64:";
        encoded += Aws::Utils::HashingUtils::Base64Encode(bytes);
        if (encoded.size() > 1024 * 1024) {
            throw std::runtime_error(
                "SQS plugin: encoded payload exceeds the 1 MiB SQS limit");
        }
        return encoded;
    }

    std::vector<unsigned char> DecodePayload(const Aws::String& body)
    {
        constexpr const char* prefix = "graftcode-base64:";
        constexpr std::size_t prefixSize = 17;
        if (body.size() < prefixSize ||
            body.compare(0, prefixSize, prefix) != 0) {
            throw std::runtime_error(
                "SQS plugin: message body has an unsupported encoding");
        }

        const Aws::Utils::ByteBuffer decoded =
            Aws::Utils::HashingUtils::Base64Decode(body.substr(prefixSize));
        if (decoded.GetLength() == 0) {
            return {};
        }
        return std::vector<unsigned char>(
            decoded.GetUnderlyingData(),
            decoded.GetUnderlyingData() + decoded.GetLength());
    }

    std::string NewCorrelationId()
    {
        thread_local std::mt19937_64 randomEngine(
            std::random_device{}() ^
            static_cast<std::mt19937_64::result_type>(
                std::chrono::steady_clock::now().time_since_epoch().count()));

        const auto first = randomEngine();
        const auto second = randomEngine();
        std::ostringstream value;
        value << std::hex << std::setfill('0')
              << std::setw(16) << first
              << std::setw(16) << second;
        return value.str();
    }

    std::string GetStringAttribute(
        const Aws::SQS::Model::Message& message,
        const char* name)
    {
        const auto& attributes = message.GetMessageAttributes();
        const auto value = attributes.find(name);
        if (value == attributes.end()) {
            return {};
        }
        return value->second.GetStringValue().c_str();
    }

    void ConfigureFifoMessage(
        Aws::SQS::Model::SendMessageRequest& request,
        const std::string& queueUrl,
        const std::string& groupId,
        const std::string& deduplicationId)
    {
        if (!IsFifoQueueUrl(queueUrl)) {
            return;
        }
        request.SetMessageGroupId(
            groupId.empty() ? "graftcode" : groupId.c_str());
        request.SetMessageDeduplicationId(deduplicationId.c_str());
    }
}
