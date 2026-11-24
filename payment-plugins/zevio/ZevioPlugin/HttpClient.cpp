#include "HttpClient.h"

#include <curl/curl.h>

#include <mutex>
#include <stdexcept>

using namespace PaymentZevio;

namespace {
	void ensureCurl() {
		static std::once_flag once;
		std::call_once(once, [] {
			if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
				throw std::runtime_error("curl_global_init failed");
		});
	}

	std::size_t writeBody(char* contents, std::size_t size, std::size_t count, void* userdata) {
		const std::size_t bytes = size * count;
		static_cast<std::string*>(userdata)->append(contents, bytes);
		return bytes;
	}
}

HttpClient::HttpClient(const std::string& baseUrl, std::string bearerToken, int timeoutMs)
	: bearerToken_(std::move(bearerToken)), timeoutMs_(timeoutMs) {
	baseUrl_ = baseUrl;
	while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
	if (baseUrl_.rfind("https://", 0) != 0 && baseUrl_.rfind("http://", 0) != 0)
		throw std::runtime_error("Invalid Zevio API URL: " + baseUrl);
	ensureCurl();
	curl_ = curl_easy_init();
	if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

HttpClient::~HttpClient() {
	if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));
}

HttpResponse HttpClient::send(const std::string& method, const std::string& path, const std::string& body) const {
	std::lock_guard<std::mutex> lock(mutex_);
	auto* easy = static_cast<CURL*>(curl_);
	curl_easy_reset(easy);

	std::string relative = path;
	while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
	const std::string url = baseUrl_ + "/" + relative;

	HttpResponse response;
	char errorBuffer[CURL_ERROR_SIZE]{};
	curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
	curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method.c_str());
	curl_easy_setopt(easy, CURLOPT_USERAGENT, "graftcode-payment-zevio/0.1");
	curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeBody);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errorBuffer);
	if (method != "GET" && method != "HEAD") {
		curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.data());
		curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
	}

	std::string authorization = "Authorization: Bearer " + bearerToken_;
	curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, authorization.c_str());
	if (!body.empty())
		headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
	curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);

	const CURLcode code = curl_easy_perform(easy);
	curl_slist_free_all(headers);
	if (code != CURLE_OK) {
		const char* detail = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(code);
		throw std::runtime_error(std::string("Zevio ") + method + " failed: " + detail);
	}
	long status = 0;
	curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
	response.status = static_cast<int>(status);
	return response;
}
