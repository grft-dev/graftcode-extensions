#include "PubSubUtil.h"

#include <nlohmann/json.hpp>

#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <openssl/err.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <utility>

namespace Graftcode::Plugins::PubSub
{
    namespace
    {
        constexpr long kConnectTimeoutMs = 5000;
        constexpr long kRpcTimeoutMs = 15000;
        constexpr long kPullTimeoutMs = 2000;
        constexpr long kMetadataTimeoutMs = 2000;
        constexpr std::size_t kMaxResponseBytes = 16 * 1024 * 1024;
        constexpr const char* kPubSubScope = "https://www.googleapis.com/auth/pubsub";
        constexpr const char* kDefaultTokenUri = "https://oauth2.googleapis.com/token";
        constexpr const char* kMetadataTokenUrl =
            "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token";

        void EnsureCurlGlobal()
        {
            static std::once_flag once;
            std::call_once(once, []() {
                curl_global_init(CURL_GLOBAL_DEFAULT);
            });
        }

        std::size_t WriteToString(
            char* contents,
            std::size_t size,
            std::size_t count,
            void* userdata)
        {
            const std::size_t bytes = size * count;
            auto* output = static_cast<std::string*>(userdata);
            if (output->size() + bytes > kMaxResponseBytes) {
                return 0;
            }
            output->append(contents, bytes);
            return bytes;
        }

        std::string Truncate(const std::string& value)
        {
            if (value.size() <= 512) {
                return value;
            }
            return value.substr(0, 512) + "...";
        }

        int Base64Value(char character)
        {
            if (character >= 'A' && character <= 'Z') {
                return character - 'A';
            }
            if (character >= 'a' && character <= 'z') {
                return character - 'a' + 26;
            }
            if (character >= '0' && character <= '9') {
                return character - '0' + 52;
            }
            if (character == '+' || character == '-') {
                return 62;
            }
            if (character == '/' || character == '_') {
                return 63;
            }
            return -1;
        }

        std::string Base64Url(const std::string& raw)
        {
            std::string encoded = Base64Encode(
                reinterpret_cast<const unsigned char*>(raw.data()),
                raw.size());
            for (char& character : encoded) {
                if (character == '+') {
                    character = '-';
                }
                else if (character == '/') {
                    character = '_';
                }
            }
            while (!encoded.empty() && encoded.back() == '=') {
                encoded.pop_back();
            }
            return encoded;
        }

        std::string OpenSslError()
        {
            const unsigned long code = ERR_get_error();
            if (code == 0) {
                return "unknown OpenSSL error";
            }
            char buffer[256];
            ERR_error_string_n(code, buffer, sizeof(buffer));
            return buffer;
        }

        struct BioFree
        {
            void operator()(BIO* bio) const { BIO_free(bio); }
        };

        struct EvpPkeyFree
        {
            void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
        };

        struct MdCtxFree
        {
            void operator()(EVP_MD_CTX* context) const { EVP_MD_CTX_free(context); }
        };

        std::string SignRs256(const std::string& input, const std::string& pem)
        {
            std::unique_ptr<BIO, BioFree> bio(BIO_new_mem_buf(
                pem.data(),
                static_cast<int>(pem.size())));
            if (!bio) {
                throw std::runtime_error("Pub/Sub plugin: failed to read private key");
            }

            std::unique_ptr<EVP_PKEY, EvpPkeyFree> key(PEM_read_bio_PrivateKey(
                bio.get(),
                nullptr,
                nullptr,
                nullptr));
            if (!key) {
                throw std::runtime_error(
                    "Pub/Sub plugin: failed to parse service-account private key: " +
                    OpenSslError());
            }

            std::unique_ptr<EVP_MD_CTX, MdCtxFree> context(EVP_MD_CTX_new());
            if (!context ||
                EVP_DigestSignInit(
                    context.get(),
                    nullptr,
                    EVP_sha256(),
                    nullptr,
                    key.get()) != 1 ||
                EVP_DigestSignUpdate(
                    context.get(),
                    input.data(),
                    input.size()) != 1) {
                throw std::runtime_error(
                    "Pub/Sub plugin: RS256 sign failed: " + OpenSslError());
            }

            std::size_t signatureSize = 0;
            if (EVP_DigestSignFinal(context.get(), nullptr, &signatureSize) != 1) {
                throw std::runtime_error(
                    "Pub/Sub plugin: RS256 sign failed: " + OpenSslError());
            }
            std::string signature(signatureSize, '\0');
            if (EVP_DigestSignFinal(
                    context.get(),
                    reinterpret_cast<unsigned char*>(signature.data()),
                    &signatureSize) != 1) {
                throw std::runtime_error(
                    "Pub/Sub plugin: RS256 sign failed: " + OpenSslError());
            }
            signature.resize(signatureSize);
            return signature;
        }

        std::int64_t EpochSeconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::string ReadFile(const std::string& path)
        {
            std::ifstream file(path);
            if (!file.good()) {
                throw std::runtime_error(
                    "Pub/Sub plugin: cannot read credentials file '" + path + "'");
            }
            std::stringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }
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

    std::string Base64Encode(const unsigned char* data, std::size_t size)
    {
        static constexpr char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        if (size == 0) {
            return {};
        }
        if (data == nullptr) {
            throw std::invalid_argument(
                "Pub/Sub plugin: null payload with non-zero length");
        }

        std::string encoded;
        encoded.reserve(((size + 2) / 3) * 4);
        std::size_t index = 0;
        while (index + 3 <= size) {
            const unsigned int value =
                (static_cast<unsigned int>(data[index]) << 16) |
                (static_cast<unsigned int>(data[index + 1]) << 8) |
                static_cast<unsigned int>(data[index + 2]);
            encoded.push_back(kTable[(value >> 18) & 63]);
            encoded.push_back(kTable[(value >> 12) & 63]);
            encoded.push_back(kTable[(value >> 6) & 63]);
            encoded.push_back(kTable[value & 63]);
            index += 3;
        }

        if (index < size) {
            unsigned int value = static_cast<unsigned int>(data[index]) << 16;
            encoded.push_back(kTable[(value >> 18) & 63]);
            if (index + 1 < size) {
                value |= static_cast<unsigned int>(data[index + 1]) << 8;
                encoded.push_back(kTable[(value >> 12) & 63]);
                encoded.push_back(kTable[(value >> 6) & 63]);
                encoded.push_back('=');
            }
            else {
                encoded.push_back(kTable[(value >> 12) & 63]);
                encoded.push_back('=');
                encoded.push_back('=');
            }
        }
        return encoded;
    }

    std::vector<unsigned char> Base64Decode(const std::string& encoded)
    {
        std::string filtered;
        filtered.reserve(encoded.size());
        for (const char character : encoded) {
            if (character == '=' || Base64Value(character) >= 0) {
                filtered.push_back(character);
            }
            else if (character == ' ' || character == '\n' || character == '\r' ||
                character == '\t') {
                continue;
            }
            else {
                throw std::runtime_error(
                    "Pub/Sub plugin: message data is not valid base64");
            }
        }

        while (!filtered.empty() && filtered.back() == '=') {
            filtered.pop_back();
        }

        std::vector<unsigned char> decoded;
        decoded.reserve(filtered.size() * 3 / 4);
        unsigned int accumulator = 0;
        int bits = 0;
        for (const char character : filtered) {
            const int value = Base64Value(character);
            if (value < 0) {
                throw std::runtime_error(
                    "Pub/Sub plugin: message data is not valid base64");
            }
            accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                decoded.push_back(
                    static_cast<unsigned char>((accumulator >> bits) & 0xFF));
            }
        }
        return decoded;
    }

    std::string BuildServiceAccountJwt(
        const std::string& clientEmail,
        const std::string& privateKeyPem,
        const std::string& tokenUri,
        std::int64_t issuedAtEpochSeconds)
    {
        if (clientEmail.empty() || privateKeyPem.empty()) {
            throw std::runtime_error(
                "Pub/Sub plugin: service account is missing client_email or private_key");
        }

        const std::string header = Base64Url(R"({"alg":"RS256","typ":"JWT"})");
        const nlohmann::json claims = {
            {"iss", clientEmail},
            {"scope", kPubSubScope},
            {"aud", tokenUri.empty() ? kDefaultTokenUri : tokenUri},
            {"iat", issuedAtEpochSeconds},
            {"exp", issuedAtEpochSeconds + 3600}
        };
        const std::string payload = Base64Url(claims.dump());
        const std::string signingInput = header + "." + payload;
        return signingInput + "." + Base64Url(SignRs256(signingInput, privateKeyPem));
    }

    PubSubSession::PubSubSession(PubSubConfig config)
        : config_(std::move(config))
    {
        EnsureCurlGlobal();
        curl_ = curl_easy_init();
        if (curl_ == nullptr) {
            throw std::runtime_error("Pub/Sub plugin: curl_easy_init failed");
        }
    }

    PubSubSession::~PubSubSession()
    {
        if (curl_ != nullptr) {
            curl_easy_cleanup(static_cast<CURL*>(curl_));
            curl_ = nullptr;
        }
    }

    std::string PubSubSession::ResourceUrl(
        const std::string& collection,
        const std::string& id) const
    {
        return EndpointBase(config_) + "/v1/projects/" + config_.projectId + "/" +
            collection + "/" + id;
    }

    PubSubSession::HttpResult PubSubSession::Request(
        const char* method,
        const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers,
        long timeoutMs,
        bool authenticate)
    {
        std::string authorization;
        if (authenticate) {
            // Token refresh performs its own request on this handle, so resolve
            // it before configuring the caller request.
            authorization = "Authorization: Bearer " + BearerToken();
        }

        auto* easy = static_cast<CURL*>(curl_);
        curl_easy_reset(easy);

        HttpResult result;
        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, timeoutMs);
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteToString);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &result.body);
        curl_easy_setopt(
            easy,
            CURLOPT_SSL_VERIFYPEER,
            config_.verifySsl ? 1L : 0L);
        curl_easy_setopt(
            easy,
            CURLOPT_SSL_VERIFYHOST,
            config_.verifySsl ? 2L : 0L);
        if (std::string(method) != "GET") {
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(
                easy,
                CURLOPT_POSTFIELDSIZE_LARGE,
                static_cast<curl_off_t>(body.size()));
        }

        bool hasContentType = false;
        for (const auto& header : headers) {
            if (header.rfind("Content-Type:", 0) == 0) {
                hasContentType = true;
                break;
            }
        }

        curl_slist* headerList = nullptr;
        if (!hasContentType) {
            headerList = curl_slist_append(
                headerList,
                "Content-Type: application/json");
        }
        if (!authorization.empty()) {
            headerList = curl_slist_append(headerList, authorization.c_str());
        }
        for (const auto& header : headers) {
            headerList = curl_slist_append(headerList, header.c_str());
        }
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headerList);

        const CURLcode code = curl_easy_perform(easy);
        curl_slist_free_all(headerList);
        if (code == CURLE_OPERATION_TIMEDOUT) {
            result.timedOut = true;
            result.error = curl_easy_strerror(code);
            return result;
        }
        if (code != CURLE_OK) {
            result.error = curl_easy_strerror(code);
            return result;
        }
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &result.status);
        return result;
    }

    void PubSubSession::ThrowIfHttpFailed(
        const char* operation,
        const HttpResult& result) const
    {
        if (result.status >= 200 && result.status < 300) {
            return;
        }
        if (result.timedOut) {
            throw PubSubHttpError(
                0,
                std::string("Pub/Sub ") + operation + " timed out");
        }
        if (result.status == 0) {
            throw PubSubHttpError(
                0,
                std::string("Pub/Sub ") + operation + " failed: " +
                    (result.error.empty() ? "connection failed" : result.error));
        }
        throw PubSubHttpError(
            result.status,
            std::string("Pub/Sub ") + operation + " failed: HTTP " +
                std::to_string(result.status) + " " + Truncate(result.body));
    }

    std::string PubSubSession::BearerToken()
    {
        if (!config_.emulatorHost.empty()) {
            return {};
        }
        if (!config_.accessToken.empty()) {
            return config_.accessToken;
        }

        const auto now = EpochSeconds();
        if (!cachedToken_.empty() && now < tokenExpiryEpoch_) {
            return cachedToken_;
        }
        if (!config_.credentialsFile.empty()) {
            RefreshFromServiceAccount();
            return cachedToken_;
        }
        if (TryMetadataToken()) {
            return cachedToken_;
        }
        throw std::runtime_error(
            "Pub/Sub plugin: no credentials. Set accessToken, credentialsFile, "
            "GOOGLE_APPLICATION_CREDENTIALS, or emulatorHost / PUBSUB_EMULATOR_HOST");
    }

    void PubSubSession::RefreshFromServiceAccount()
    {
        const auto json = nlohmann::json::parse(ReadFile(config_.credentialsFile));
        if (!json.is_object() ||
            !json.contains("client_email") ||
            !json["client_email"].is_string() ||
            !json.contains("private_key") ||
            !json["private_key"].is_string()) {
            throw std::runtime_error(
                "Pub/Sub plugin: credentials file must contain client_email and private_key");
        }

        const std::string email = json["client_email"].get<std::string>();
        const std::string key = json["private_key"].get<std::string>();
        const std::string tokenUri = json.value("token_uri", kDefaultTokenUri);
        const auto issuedAt = EpochSeconds();
        const std::string jwt = BuildServiceAccountJwt(email, key, tokenUri, issuedAt);

        const std::string form =
            std::string("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=") +
            jwt;
        const HttpResult result = Request(
            "POST",
            tokenUri,
            form,
            {"Content-Type: application/x-www-form-urlencoded"},
            kRpcTimeoutMs,
            false);
        ThrowIfHttpFailed("token exchange", result);

        const auto parsed = nlohmann::json::parse(result.body);
        if (!parsed.is_object() ||
            !parsed.contains("access_token") ||
            !parsed["access_token"].is_string()) {
            throw std::runtime_error(
                "Pub/Sub plugin: token endpoint did not return access_token");
        }
        cachedToken_ = parsed["access_token"].get<std::string>();
        const auto expiresIn = parsed.value("expires_in", 3600);
        tokenExpiryEpoch_ = issuedAt + (expiresIn > 90 ? expiresIn - 60 : expiresIn);
    }

    bool PubSubSession::TryMetadataToken()
    {
        const HttpResult result = Request(
            "GET",
            kMetadataTokenUrl,
            "",
            {"Metadata-Flavor: Google"},
            kMetadataTimeoutMs,
            false);
        if (result.status != 200) {
            return false;
        }
        const auto parsed = nlohmann::json::parse(result.body, nullptr, false);
        if (parsed.is_discarded() ||
            !parsed.is_object() ||
            !parsed.contains("access_token") ||
            !parsed["access_token"].is_string()) {
            return false;
        }
        cachedToken_ = parsed["access_token"].get<std::string>();
        const auto expiresIn = parsed.value("expires_in", 3600);
        tokenExpiryEpoch_ = EpochSeconds() + (expiresIn > 90 ? expiresIn - 60 : expiresIn);
        return true;
    }

    void PubSubSession::EnsureTopic(const std::string& topic)
    {
        const nlohmann::json body = {
            {"name", "projects/" + config_.projectId + "/topics/" + topic}
        };
        const HttpResult result = Request(
            "PUT",
            ResourceUrl("topics", topic),
            body.dump(),
            {},
            kRpcTimeoutMs,
            config_.emulatorHost.empty());
        if (result.status == 409 ||
            (result.status >= 400 &&
                result.body.find("ALREADY_EXISTS") != std::string::npos)) {
            return;
        }
        ThrowIfHttpFailed("CreateTopic", result);
    }

    void PubSubSession::EnsureSubscription(
        const std::string& subscription,
        const std::string& topic)
    {
        const nlohmann::json body = {
            {"name",
                "projects/" + config_.projectId + "/subscriptions/" + subscription},
            {"topic", "projects/" + config_.projectId + "/topics/" + topic},
            {"ackDeadlineSeconds", config_.ackDeadlineSeconds}
        };
        const HttpResult result = Request(
            "PUT",
            ResourceUrl("subscriptions", subscription),
            body.dump(),
            {},
            kRpcTimeoutMs,
            config_.emulatorHost.empty());
        if (result.status == 409 ||
            (result.status >= 400 &&
                result.body.find("ALREADY_EXISTS") != std::string::npos)) {
            return;
        }
        ThrowIfHttpFailed("CreateSubscription", result);
    }

    void PubSubSession::Publish(
        const std::string& topic,
        const unsigned char* data,
        std::size_t size,
        const std::map<std::string, std::string>& attributes)
    {
        if (size > MaxPayloadBytes) {
            throw std::runtime_error(
                "Pub/Sub plugin: payload exceeds the 10 MiB Pub/Sub limit");
        }

        nlohmann::json attributeJson = nlohmann::json::object();
        for (const auto& [key, value] : attributes) {
            attributeJson[key] = value;
        }
        const nlohmann::json body = {
            {"messages", nlohmann::json::array({
                {
                    {"data", Base64Encode(data, size)},
                    {"attributes", std::move(attributeJson)}
                }
            })}
        };
        const HttpResult result = Request(
            "POST",
            ResourceUrl("topics", topic) + ":publish",
            body.dump(),
            {},
            kRpcTimeoutMs,
            config_.emulatorHost.empty());
        ThrowIfHttpFailed(
            ("Publish " + topic).c_str(),
            result);
    }

    std::vector<PulledMessage> PubSubSession::Pull(
        const std::string& subscription,
        std::uint32_t maxMessages)
    {
        const nlohmann::json body = {
            {"maxMessages", maxMessages},
            {"returnImmediately", true}
        };
        const HttpResult result = Request(
            "POST",
            ResourceUrl("subscriptions", subscription) + ":pull",
            body.dump(),
            {},
            kPullTimeoutMs,
            config_.emulatorHost.empty());
        if (result.timedOut) {
            return {};
        }
        ThrowIfHttpFailed(("Pull " + subscription).c_str(), result);

        const auto parsed = nlohmann::json::parse(result.body, nullptr, false);
        std::vector<PulledMessage> messages;
        if (parsed.is_discarded() || !parsed.is_object()) {
            return messages;
        }
        const auto received = parsed.find("receivedMessages");
        if (received == parsed.end() || !received->is_array()) {
            return messages;
        }

        for (const auto& item : *received) {
            if (!item.is_object()) {
                continue;
            }
            PulledMessage message;
            if (item.contains("ackId") && item["ackId"].is_string()) {
                message.ackId = item["ackId"].get<std::string>();
            }
            const auto payload = item.find("message");
            if (payload != item.end() && payload->is_object()) {
                if (payload->contains("data") && (*payload)["data"].is_string()) {
                    message.data = Base64Decode((*payload)["data"].get<std::string>());
                }
                if (payload->contains("attributes") &&
                    (*payload)["attributes"].is_object()) {
                    for (auto it = (*payload)["attributes"].begin();
                        it != (*payload)["attributes"].end();
                        ++it) {
                        if (it->is_string()) {
                            message.attributes[it.key()] = it->get<std::string>();
                        }
                    }
                }
            }
            messages.push_back(std::move(message));
        }
        return messages;
    }

    void PubSubSession::Acknowledge(
        const std::string& subscription,
        const std::vector<std::string>& ackIds)
    {
        if (ackIds.empty()) {
            return;
        }
        const nlohmann::json body = {{"ackIds", ackIds}};
        const HttpResult result = Request(
            "POST",
            ResourceUrl("subscriptions", subscription) + ":acknowledge",
            body.dump(),
            {},
            kRpcTimeoutMs,
            config_.emulatorHost.empty());
        ThrowIfHttpFailed(("Acknowledge " + subscription).c_str(), result);
    }
}
