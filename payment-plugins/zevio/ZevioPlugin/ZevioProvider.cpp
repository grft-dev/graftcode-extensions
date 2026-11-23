// payment.zevio – native payment provider plugin for Graftcode Gateway (C ABI: PaymentProviderAbi.h).
//
// Knows only Zevio (https://api.<env>.zevio.io/api/v1, Bearer API key):
//  - createSession: offer-based checkout link. Service reference -> offer qrId (config "offers"),
//    GET /qr/{qrId} must be ACTIVE with exactly the quoted price, then POST /payments/{qrId}
//    with metadata.challenge_id = sessionRef. Returns paymentUrl (destinationLink) and shortUrl (link).
//  - verify: GET /payments (newest first, filtered by offer and creation time) and find the payment whose
//    metadata.challenge_id == sessionRef. SUCCESS = paid; PENDING/FAILED (or none yet) = pending; others = failed.
// Knows nothing about x402/MCP – that is Graftcode Gateway core. Never logs or returns the API key.

#include "PaymentProviderAbi.h"
#include "WinHttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define PAYMENT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PAYMENT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using json = nlohmann::json;
using namespace PaymentZevio;

namespace {
	constexpr const char* kPluginId = "payment.zevio";
	constexpr const char* kSandboxUrl = "https://api.sandbox.zevio.io/api/v1";

	char* dupString(const std::string& s) {
		char* out = static_cast<char*>(std::malloc(s.size() + 1));
		if (!out) return nullptr;
		std::memcpy(out, s.data(), s.size());
		out[s.size()] = '\0';
		return out;
	}
	char* dupJson(const json& j) { return dupString(j.dump()); }

	std::string lower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}
	std::string upper(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return s;
	}
	std::string urlEncode(const std::string& s) {
		static const char* hex = "0123456789ABCDEF";
		std::string out;
		for (unsigned char c : s) {
			if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
			else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
		}
		return out;
	}
	/** JSON number or decimal string -> minor units (grosze). */
	long long toMinor(const json& v) {
		if (v.is_number_integer()) return v.get<long long>() * 100;
		if (v.is_number()) return std::llround(v.get<double>() * 100.0);
		if (v.is_string()) {
			const std::string s = v.get<std::string>();
			const auto dot = s.find('.');
			std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
			while (frac.size() < 2) frac += '0';
			return std::stoll(s.substr(0, dot)) * 100 + std::stoll(frac.substr(0, 2));
		}
		throw std::runtime_error("amount is not a number");
	}
	std::string formatMinor(long long minor) {
		std::string frac = std::to_string(minor % 100);
		if (frac.size() < 2) frac = "0" + frac;
		return std::to_string(minor / 100) + "." + frac;
	}
	/** "2026-11-23T12:30:00Z" minus N seconds -> "2026-11-23T12:20:00.000Z" (Zevio 'from' filter). */
	std::string isoMinus(const std::string& iso, long long seconds) {
		std::tm tm{};
		if (iso.size() < 19) return "";
		tm.tm_year = std::stoi(iso.substr(0, 4)) - 1900;
		tm.tm_mon = std::stoi(iso.substr(5, 2)) - 1;
		tm.tm_mday = std::stoi(iso.substr(8, 2));
		tm.tm_hour = std::stoi(iso.substr(11, 2));
		tm.tm_min = std::stoi(iso.substr(14, 2));
		tm.tm_sec = std::stoi(iso.substr(17, 2));
#ifdef _WIN32
		std::time_t t = _mkgmtime(&tm);
#else
		std::time_t t = timegm(&tm);
#endif
		t -= static_cast<std::time_t>(seconds);
		std::tm out{};
#ifdef _WIN32
		gmtime_s(&out, &t);
#else
		gmtime_r(&t, &out);
#endif
		char buf[40];
		std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", &out);
		return buf;
	}

	json validate(const json& config) {
		json errors = json::array();
		if (!config.is_object()) { errors.push_back("config must be an object"); return errors; }
		const std::string env = config.value("environment", "sandbox");
		if (env != "sandbox" && env != "production") errors.push_back("environment must be 'sandbox' or 'production'");
		if (env == "production" && config.value("apiUrl", "").empty())
			errors.push_back("environment 'production' requires an explicit apiUrl");
		if (!config.contains("apiKey") || !config["apiKey"].is_string() || config["apiKey"].get<std::string>().empty())
			errors.push_back("apiKey is required (use {\"secretRef\":\"env:ZEVIO_API_KEY\"} in the GG payment config)");
		if (!config.contains("offers") || !config["offers"].is_object() || config["offers"].empty())
			errors.push_back("offers must map service references to Zevio offer ids, e.g. {\"haircut\":\"qr_...\"}");
		else
			for (auto& [k, v] : config["offers"].items())
				if (!v.is_string() || v.get<std::string>().rfind("qr_", 0) != 0)
					errors.push_back("offers." + k + " must be a Zevio offer id (qr_...)");
		return errors;
	}

	json errorResult(const std::string& code, const std::string& message) {
		return json{ {"ok", false}, {"error", { {"code", code}, {"message", message} }} };
	}

	/** Zevio error body is {"errorKey":"..."}; keep it short and never echo request headers. */
	std::string describe(const HttpResponse& r) {
		json body = json::parse(r.body, nullptr, false);
		std::string key = (!body.is_discarded() && body.is_object()) ? body.value("errorKey", "") : "";
		if (key.empty()) key = r.body.substr(0, 200);
		return "HTTP " + std::to_string(r.status) + " " + key;
	}
}

struct PaymentProviderInstance {
	std::unique_ptr<WinHttpClient> http;
	json offers;
	std::string customerEmail;
	std::string language;
	std::string environment;

	json getJson(const std::string& path) const {
		const HttpResponse r = http->send("GET", path, "");
		if (r.status < 200 || r.status >= 300) throw std::runtime_error("Zevio GET /" + path.substr(0, path.find('?')) + " -> " + describe(r));
		return json::parse(r.body);
	}

	json createSession(const json& req) const {
		const std::string reference = req.value("reference", "");
		if (!offers.contains(reference))
			return errorResult("offer_not_configured", "No Zevio offer configured for reference '" + reference + "'");
		const std::string qrId = offers[reference].get<std::string>();

		// The offer sets the price (POST /payments/{qrId} has no amount): it must equal the merchant quote.
		const json offer = getJson("qr/" + urlEncode(qrId));
		if (lower(offer.value("status", "")) != "active")
			return errorResult("offer_inactive", "Zevio offer " + qrId + " is not active");
		const long long offerMinor = toMinor(offer.contains("totalAmount") ? offer["totalAmount"] : offer["amount"]);
		const std::string offerCurrency = upper(offer.value("currency", ""));
		const long long quoteMinor = toMinor(req["amount"]);
		if (offerMinor != quoteMinor || offerCurrency != upper(req.value("currency", "")))
			return errorResult("price_mismatch", "Zevio offer " + qrId + " costs " + formatMinor(offerMinor) + " " + offerCurrency
				+ ", the merchant quoted " + formatMinor(quoteMinor) + " " + upper(req.value("currency", "")));

		json metadata = req.value("metadata", json::object());
		metadata["challenge_id"] = req.value("sessionRef", "");
		const json body{
			{"customer", { {"email", customerEmail}, {"language", language} }},
			{"activationLink", false},
			{"metadata", metadata}
		};
		const HttpResponse r = http->send("POST", "payments/" + urlEncode(qrId), body.dump());
		if (r.status < 200 || r.status >= 300)
			return errorResult("zevio_error", "POST /payments/" + qrId + " -> " + describe(r));
		const json created = json::parse(r.body);
		const std::string link = created.value("link", "");
		const std::string destination = created.value("destinationLink", "");
		if (link.empty() && destination.empty())
			return errorResult("zevio_error", "Zevio returned no payment link");
		return json{
			{"ok", true},
			{"scheme", "zevio-checkout"},
			{"sessionId", req.value("sessionRef", "")},
			{"offerId", qrId},
			{"paymentUrl", destination.empty() ? link : destination},
			{"shortUrl", link.empty() ? destination : link}
		};
	}

	json verify(const json& req) const {
		const std::string sessionRef = req.value("sessionRef", "");
		std::string url = "payments?limit=100&sortBy=createdAt&sortOrder=desc";
		const std::string reference = req.value("reference", "");
		if (offers.contains(reference)) url += "&qrId=" + urlEncode(offers[reference].get<std::string>());
		const std::string from = isoMinus(req.value("issuedAt", ""), 600);
		if (!from.empty()) url += "&from=" + urlEncode(from);

		json list;
		try { list = getJson(url); }
		catch (const std::exception& e) {
			return json{ {"status", "failed"}, {"error", "provider_unavailable"}, {"detail", e.what()} };
		}
		const json* items = nullptr;
		if (list.is_array()) items = &list;
		else for (const char* key : { "payments", "items", "data" })
			if (list.contains(key) && list[key].is_array()) { items = &list[key]; break; }
		if (!items) return json{ {"status", "failed"}, {"error", "provider_unavailable"}, {"detail", "Unexpected GET /payments response"} };

		// Zevio has no "link status": each attempt is a payment. SUCCESS wins, otherwise the newest attempt.
		const json* newest = nullptr;
		for (const auto& p : *items) {
			if (!p.is_object() || !p.contains("metadata") || !p["metadata"].is_object()) continue;
			if (p["metadata"].value("challenge_id", "") != sessionRef) continue;
			if (lower(p.value("status", "")) == "success") { newest = &p; break; }
			if (!newest) newest = &p;
		}
		if (!newest)
			return json{ {"status", "pending"}, {"detail", "Zevio has no payment for this challenge yet - pay at paymentUrl."} };
		const std::string status = lower(newest->value("status", ""));
		if (status == "pending" || status == "failed")
			return json{ {"status", "pending"}, {"detail", "Zevio payment status is " + status + " - finish (or retry) the payment at paymentUrl."} };
		if (status != "success")
			return json{ {"status", "failed"}, {"error", "payment_not_completed:" + status}, {"detail", "Zevio payment status is " + status} };

		const long long paidMinor = toMinor(newest->contains("totalAmount") ? (*newest)["totalAmount"] : (*newest)["amount"]);
		const std::string currency = upper(newest->value("currency", ""));
		if (currency != upper(req.value("currency", "")))
			return json{ {"status", "failed"}, {"error", "currency_mismatch"}, {"detail", "Paid in " + currency} };
		if (paidMinor < toMinor(req["amount"]))
			return json{ {"status", "failed"}, {"error", "amount_too_low"}, {"detail", "Paid " + formatMinor(paidMinor)} };
		return json{
			{"status", "paid"},
			{"paymentId", newest->value("paymentId", newest->value("id", ""))},
			{"amount", formatMinor(paidMinor)},
			{"currency", currency},
			{"settlementStatus", "Settled"},
			{"method", newest->value("method", "")}
		};
	}
};

PAYMENT_PLUGIN_EXPORT uint32_t PaymentProviderAbiVersion(void) { return GG_PAYMENT_PROVIDER_ABI_VERSION; }

PAYMENT_PLUGIN_EXPORT const char* PaymentProviderId(void) { return kPluginId; }

PAYMENT_PLUGIN_EXPORT char* PaymentProviderCapabilities(void) {
	return dupJson(json{
		{"id", kPluginId},
		{"provider", "zevio"},
		{"scheme", "zevio-checkout"},
		{"interactions", json::array({ "checkout_link" })},
		{"pricing", json::array({ "offer" })},
		{"currencies", json::array({ "PLN" })},
		{"verification", "poll"},
		{"webhooks", false},
		{"environments", json::array({ "sandbox", "production" })}
	});
}

PAYMENT_PLUGIN_EXPORT char* PaymentProviderConfigSchema(void) {
	return dupJson(json{
		{"$schema", "https://json-schema.org/draft/2020-12/schema"},
		{"type", "object"},
		{"required", json::array({ "apiKey", "offers" })},
		{"properties", {
			{"environment", { {"enum", json::array({ "sandbox", "production" })}, {"default", "sandbox"} }},
			{"apiUrl", { {"type", "string"}, {"description", "Override of the Zevio API base URL (required for production)"} }},
			{"apiKey", { {"type", "string"}, {"description", "Zevio API key (Bearer). Pass as secretRef in GG config; never logged."}, {"writeOnly", true} }},
			{"customerEmail", { {"type", "string"}, {"default", "poc-agent@example.com"} }},
			{"language", { {"enum", json::array({ "PL", "EN" })}, {"default", "PL"} }},
			{"timeoutMs", { {"type", "integer"}, {"default", 30000} }},
			{"offers", { {"type", "object"}, {"additionalProperties", { {"type", "string"}, {"pattern", "^qr_"} }},
				{"description", "Merchant quote reference -> Zevio offer qrId"} }}
		}}
	});
}

PAYMENT_PLUGIN_EXPORT char* PaymentProviderValidateConfig(const char* configJson) {
	const json config = json::parse(configJson ? configJson : "", nullptr, false);
	if (config.is_discarded()) return dupJson(json{ {"ok", false}, {"errors", json::array({ "config is not valid JSON" })} });
	const json errors = validate(config);
	return dupJson(errors.empty() ? json{ {"ok", true} } : json{ {"ok", false}, {"errors", errors} });
}

PAYMENT_PLUGIN_EXPORT PaymentProviderInstance* CreatePaymentProvider(const char* configJson, char** errorJson) {
	try {
		const json config = json::parse(configJson ? configJson : "");
		const json errors = validate(config);
		if (!errors.empty()) throw std::runtime_error(errors.dump());
		auto instance = std::make_unique<PaymentProviderInstance>();
		instance->environment = config.value("environment", "sandbox");
		const std::string baseUrl = config.value("apiUrl", "").empty() ? std::string(kSandboxUrl) : config.value("apiUrl", "");
		instance->http = std::make_unique<WinHttpClient>(baseUrl, config["apiKey"].get<std::string>(), config.value("timeoutMs", 30000));
		instance->offers = config["offers"];
		instance->customerEmail = config.value("customerEmail", "poc-agent@example.com");
		instance->language = config.value("language", "PL");
		return instance.release();
	}
	catch (const std::exception& e) {
		if (errorJson) *errorJson = dupJson(json{ {"error", e.what()} });
		return nullptr;
	}
}

PAYMENT_PLUGIN_EXPORT void DestroyPaymentProvider(PaymentProviderInstance* instance) {
	delete instance;
}

PAYMENT_PLUGIN_EXPORT char* PaymentProviderCreateSession(PaymentProviderInstance* instance, const char* requestJson) {
	try {
		if (!instance) return dupJson(errorResult("not_initialized", "provider instance is null"));
		return dupJson(instance->createSession(json::parse(requestJson ? requestJson : "")));
	}
	catch (const std::exception& e) {
		return dupJson(errorResult("zevio_error", e.what()));
	}
}

PAYMENT_PLUGIN_EXPORT char* PaymentProviderVerify(PaymentProviderInstance* instance, const char* requestJson) {
	try {
		if (!instance) return dupJson(json{ {"status", "failed"}, {"error", "not_initialized"} });
		return dupJson(instance->verify(json::parse(requestJson ? requestJson : "")));
	}
	catch (const std::exception& e) {
		return dupJson(json{ {"status", "failed"}, {"error", "provider_unavailable"}, {"detail", e.what()} });
	}
}

PAYMENT_PLUGIN_EXPORT void PaymentProviderFreeBuffer(void* buffer) {
	std::free(buffer);
}
