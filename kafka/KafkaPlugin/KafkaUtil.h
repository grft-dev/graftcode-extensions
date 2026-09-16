#pragma once

#include <cstdint>
#include <random>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#if __has_include(<librdkafka/rdkafkacpp.h>)
#include <librdkafka/rdkafkacpp.h>
#elif __has_include(<rdkafkacpp.h>)
#include <rdkafkacpp.h>
#else
#error "librdkafka C++ header not found (expected librdkafka/rdkafkacpp.h or rdkafkacpp.h)"
#endif

namespace KafkaPluginUtil {

inline std::string makeUuidV4() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    const std::uint64_t a = dist(gen);
    const std::uint64_t b = dist(gen);

    // RFC 4122 variant 1, version 4
    const std::uint64_t time_low = a & 0xFFFFFFFFULL;
    const std::uint64_t time_mid = (a >> 32) & 0xFFFFULL;
    const std::uint64_t time_hi = ((a >> 48) & 0x0FFFULL) | 0x4000ULL;
    const std::uint64_t clock_seq = ((b >> 48) & 0x3FFFULL) | 0x8000ULL;
    const std::uint64_t node = b & 0xFFFFFFFFFFFFULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << time_low << '-'
        << std::setw(4) << time_mid << '-'
        << std::setw(4) << time_hi << '-'
        << std::setw(4) << clock_seq << '-'
        << std::setw(12) << node;
    return oss.str();
}

inline std::string headerValue(const RdKafka::Headers* headers, const std::string& key) {
    if (!headers) {
        return {};
    }
    const auto result = headers->get(key);
    if (result.empty() || !result[0].value() || result[0].value_size() == 0) {
        return {};
    }
    return std::string(static_cast<const char*>(result[0].value()), result[0].value_size());
}

inline RdKafka::Headers* makeRpcHeaders(const std::string& correlationId,
                                        const std::string& replyTo) {
    RdKafka::Headers* headers = RdKafka::Headers::create();
    headers->add("correlation-id", correlationId);
    if (!replyTo.empty()) {
        headers->add("reply-to", replyTo);
    }
    return headers;
}

inline void setConfOrThrow(RdKafka::Conf* conf, const std::string& key, const std::string& value) {
    std::string errstr;
    if (conf->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("librdkafka conf set '" + key + "' failed: " + errstr);
    }
}

}  // namespace KafkaPluginUtil
