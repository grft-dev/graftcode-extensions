#include "SqsUtil.h"

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/Array.h>
#include <aws/core/utils/HashingUtils.h>
#include <aws/sqs/SQSClient.h>

#include <aws/core/http/Scheme.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#if !defined(_WIN32)
#include <unistd.h>
#endif
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

        void DisableImdsLookupForEmulator()
        {
            // Local emulators have no EC2 metadata service. Leaving the default
            // provider chain enabled adds multi-second timeouts when static
            // credentials were not supplied.
#if defined(_WIN32)
            char* existing = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&existing, &length, "AWS_EC2_METADATA_DISABLED") != 0 ||
                existing == nullptr) {
                _putenv_s("AWS_EC2_METADATA_DISABLED", "true");
            }
            std::free(existing);
#else
            setenv("AWS_EC2_METADATA_DISABLED", "true", 0);
#endif
        }

        bool IsValidBase64(const Aws::String& value)
        {
            if (value.empty()) {
                return true;
            }
            if (value.size() % 4 != 0) {
                return false;
            }

            bool padding = false;
            for (const char character : value) {
                if (character == '=') {
                    padding = true;
                    continue;
                }
                if (padding) {
                    return false;
                }
                const bool alphabet =
                    std::isalnum(static_cast<unsigned char>(character)) ||
                    character == '+' ||
                    character == '/';
                if (!alphabet) {
                    return false;
                }
            }
            return true;
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
        // requestTimeoutMs is the Windows read timeout and the Curl low-speed
        // window. httpRequestTimeoutMs is the Curl total transfer timeout.
        // Both must outlive SQS long polling.
        const long timeoutMs = static_cast<long>(
            (std::max)(
                std::uint32_t{ 30000 },
                (config.waitTimeSeconds + 5) * 1000));
        clientConfig.requestTimeoutMs = timeoutMs;
        clientConfig.httpRequestTimeoutMs = timeoutMs;

        const ParsedEndpoint endpoint =
            ParseEndpointOverride(config.endpointOverride);
        if (endpoint.overridden) {
            DisableImdsLookupForEmulator();
            clientConfig.scheme = endpoint.useHttps
                ? Aws::Http::Scheme::HTTPS
                : Aws::Http::Scheme::HTTP;
            clientConfig.endpointOverride = endpoint.authority.c_str();
            if (!endpoint.useHttps) {
                clientConfig.verifySSL = false;
            }
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
        if (encoded.size() > MaxEncodedBodyBytes) {
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

        const Aws::String encoded = body.substr(prefixSize);
        if (!IsValidBase64(encoded)) {
            throw std::runtime_error(
                "SQS plugin: message body is not valid base64");
        }

        const Aws::Utils::ByteBuffer decoded =
            Aws::Utils::HashingUtils::Base64Decode(encoded);
        if (decoded.GetLength() == 0) {
            if (!encoded.empty()) {
                throw std::runtime_error(
                    "SQS plugin: failed to decode message body");
            }
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
