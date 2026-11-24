#include "HttpClient.h"

#include <curl/curl.h>

#include <cctype>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#endif

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

	struct EasyHandle {
		CURL* handle{ nullptr };
		EasyHandle() : handle(curl_easy_init()) {}
		~EasyHandle() { if (handle) curl_easy_cleanup(handle); }
		EasyHandle(const EasyHandle&) = delete;
		EasyHandle& operator=(const EasyHandle&) = delete;
	};

	struct Slist {
		curl_slist* list{ nullptr };
		Slist() = default;
		~Slist() { curl_slist_free_all(list); }
		Slist(const Slist&) = delete;
		Slist& operator=(const Slist&) = delete;
	};

#if defined(_WIN32)
	std::string lower(std::string s) {
		for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	std::wstring widen(const std::string& s) {
		if (s.empty()) return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
		return w;
	}

	std::string narrow(const wchar_t* w) {
		if (!w || !*w) return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1) return {};
		std::string s(static_cast<std::size_t>(n - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
		return s;
	}

	struct WinHttpStrings {
		wchar_t* a{ nullptr };
		wchar_t* b{ nullptr };
		~WinHttpStrings() {
			if (a) GlobalFree(a);
			if (b) GlobalFree(b);
		}
	};

	std::string hostOf(const std::string& url) {
		const auto scheme = url.find("://");
		std::size_t start = scheme == std::string::npos ? 0 : scheme + 3;
		if (start < url.size() && url[start] == '[') {
			const auto end = url.find(']', start);
			return end == std::string::npos ? url.substr(start) : url.substr(start, end - start + 1);
		}
		const auto end = url.find_first_of(":/?", start);
		return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
	}

	std::string schemeOf(const std::string& url) {
		const auto end = url.find("://");
		return end == std::string::npos ? "https" : lower(url.substr(0, end));
	}

	bool globMatch(const char* pattern, const char* text) {
		for (;;) {
			if (*pattern == '*') {
				while (*pattern == '*') ++pattern;
				if (*pattern == '\0') return true;
				for (; *text != '\0'; ++text)
					if (globMatch(pattern, text)) return true;
				return false;
			}
			if (*pattern == '\0') return *text == '\0';
			if (*text == '\0') return false;
			if (std::tolower(static_cast<unsigned char>(*pattern)) != std::tolower(static_cast<unsigned char>(*text)))
				return false;
			++pattern;
			++text;
		}
	}

	bool bypasses(const std::string& host, const std::wstring& list) {
		const std::string bypass = lower(narrow(list.c_str()));
		const std::string name = lower(host);
		std::size_t i = 0;
		while (i < bypass.size()) {
			while (i < bypass.size() && (bypass[i] == ';' || bypass[i] == ' ' || bypass[i] == '\t')) ++i;
			if (i >= bypass.size()) break;
			const std::size_t start = i;
			while (i < bypass.size() && bypass[i] != ';' && bypass[i] != ' ' && bypass[i] != '\t') ++i;
			const std::string entry = bypass.substr(start, i - start);
			if (entry.empty()) continue;
			if (entry == "<local>") {
				if (name.find('.') == std::string::npos) return true;
				continue;
			}
			if (entry.find('*') != std::string::npos) {
				if (globMatch(entry.c_str(), name.c_str())) return true;
				continue;
			}
			if (name == entry) return true;
			if (name.size() > entry.size() && name[name.size() - entry.size() - 1] == '.'
				&& name.compare(name.size() - entry.size(), entry.size(), entry) == 0)
				return true;
		}
		return false;
	}

	/** WinHTTP proxy token -> libcurl proxy URL. A bare host:port is an HTTP proxy. */
	std::string curlProxyUrl(std::string value) {
		const auto scheme = value.find("://");
		if (scheme != std::string::npos) return value;
		return "http://" + value;
	}

	std::string pickProxy(const std::wstring& list, const std::string& targetScheme) {
		const std::string text = narrow(list.c_str());
		std::string generic;
		std::string matched;
		std::size_t i = 0;
		while (i < text.size()) {
			while (i < text.size() && (text[i] == ';' || text[i] == ' ' || text[i] == '\t')) ++i;
			if (i >= text.size()) break;
			const std::size_t start = i;
			while (i < text.size() && text[i] != ';' && text[i] != ' ' && text[i] != '\t') ++i;
			const std::string token = text.substr(start, i - start);
			if (token.empty()) continue;
			const auto eq = token.find('=');
			if (eq == std::string::npos) {
				if (generic.empty()) generic = curlProxyUrl(token);
				continue;
			}
			if (lower(token.substr(0, eq)) == targetScheme && matched.empty())
				matched = curlProxyUrl(token.substr(eq + 1));
		}
		return matched.empty() ? generic : matched;
	}

	bool readProxyInfo(const WINHTTP_PROXY_INFO& info, const std::string& url, std::string& proxy) {
		if (info.dwAccessType != WINHTTP_ACCESS_TYPE_NAMED_PROXY || !info.lpszProxy || !*info.lpszProxy)
			return true;
		if (info.lpszProxyBypass && bypasses(hostOf(url), info.lpszProxyBypass))
			return true;
		proxy = pickProxy(info.lpszProxy, schemeOf(url));
		return true;
	}

	HINTERNET proxySession() {
		static HINTERNET session = [] {
			HINTERNET opened = WinHttpOpen(L"graftcode-payment-zevio/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
				WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (opened)
				WinHttpSetTimeouts(opened, 5000, 5000, 5000, 5000);
			return opened;
		}();
		return session;
	}

	/**
	 * nullopt: Windows did not answer; libcurl may use proxy environment variables.
	 * empty: direct connection.
	 * otherwise: a libcurl proxy URL.
	 */
	std::optional<std::string> windowsProxyFor(const std::string& url) {
		WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{};
		const bool haveIe = WinHttpGetIEProxyConfigForCurrentUser(&ie) == TRUE;
		WinHttpStrings ieFree;
		ieFree.a = ie.lpszProxy;
		ieFree.b = ie.lpszProxyBypass;
		WinHttpStrings pacFree;
		pacFree.a = ie.lpszAutoConfigUrl;

		const bool tryAuto = !haveIe || ie.fAutoDetect || ie.lpszAutoConfigUrl != nullptr;
		if (tryAuto) {
			if (HINTERNET session = proxySession()) {
				WINHTTP_AUTOPROXY_OPTIONS options{};
				options.fAutoLogonIfChallenged = TRUE;
				options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
				if (!haveIe || ie.fAutoDetect)
					options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
				if (ie.lpszAutoConfigUrl) {
					options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
					options.lpszAutoConfigUrl = ie.lpszAutoConfigUrl;
				}
				WINHTTP_PROXY_INFO info{};
				const bool resolved = options.dwFlags != 0
					&& WinHttpGetProxyForUrl(session, widen(url).c_str(), &options, &info) == TRUE;
				if (resolved) {
					std::string proxy;
					readProxyInfo(info, url, proxy);
					if (info.lpszProxy) GlobalFree(info.lpszProxy);
					if (info.lpszProxyBypass) GlobalFree(info.lpszProxyBypass);
					return proxy;
				}
			}
		}

		if (haveIe && ie.lpszProxy && *ie.lpszProxy) {
			if (ie.lpszProxyBypass && bypasses(hostOf(url), ie.lpszProxyBypass))
				return std::string{};
			return pickProxy(ie.lpszProxy, schemeOf(url));
		}
		if (haveIe)
			return std::string{};

		WINHTTP_PROXY_INFO def{};
		if (!WinHttpGetDefaultProxyConfiguration(&def))
			return std::nullopt;
		std::string proxy;
		readProxyInfo(def, url, proxy);
		if (def.lpszProxy) GlobalFree(def.lpszProxy);
		if (def.lpszProxyBypass) GlobalFree(def.lpszProxyBypass);
		return proxy;
	}

	void applyWindowsProxy(CURL* easy, const std::string& url, std::string& storage) {
		const std::optional<std::string> proxy = windowsProxyFor(url);
		if (!proxy) return;
		storage = *proxy;
		curl_easy_setopt(easy, CURLOPT_PROXY, storage.c_str());
	}
#endif
}

HttpClient::HttpClient(const std::string& baseUrl, std::string bearerToken, int timeoutMs)
	: bearerToken_(std::move(bearerToken)), timeoutMs_(timeoutMs) {
	baseUrl_ = baseUrl;
	while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
	if (baseUrl_.rfind("https://", 0) != 0 && baseUrl_.rfind("http://", 0) != 0)
		throw std::runtime_error("Invalid Zevio API URL: " + baseUrl);
	ensureCurl();
}

HttpResponse HttpClient::send(const std::string& method, const std::string& path, const std::string& body) const {
	ensureCurl();
	EasyHandle easy;
	if (!easy.handle) throw std::runtime_error("curl_easy_init failed");

	std::string relative = path;
	while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
	const std::string url = baseUrl_ + "/" + relative;

	HttpResponse response;
	char errorBuffer[CURL_ERROR_SIZE]{};
	curl_easy_setopt(easy.handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(easy.handle, CURLOPT_CUSTOMREQUEST, method.c_str());
	curl_easy_setopt(easy.handle, CURLOPT_USERAGENT, "graftcode-payment-zevio/0.1");
	curl_easy_setopt(easy.handle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(easy.handle, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(easy.handle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(easy.handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs_));
	curl_easy_setopt(easy.handle, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(easy.handle, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, writeBody);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(easy.handle, CURLOPT_ERRORBUFFER, errorBuffer);
	if (method != "GET" && method != "HEAD") {
		curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDS, body.data());
		curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
	}

#if defined(_WIN32)
	std::string proxy;
	applyWindowsProxy(easy.handle, url, proxy);
#endif

	std::string authorization = "Authorization: Bearer " + bearerToken_;
	Slist headers;
	headers.list = curl_slist_append(headers.list, "Accept: application/json");
	headers.list = curl_slist_append(headers.list, authorization.c_str());
	if (!body.empty())
		headers.list = curl_slist_append(headers.list, "Content-Type: application/json; charset=utf-8");
	curl_easy_setopt(easy.handle, CURLOPT_HTTPHEADER, headers.list);

	const CURLcode code = curl_easy_perform(easy.handle);
	if (code != CURLE_OK) {
		const char* detail = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(code);
		throw std::runtime_error(std::string("Zevio ") + method + " failed: " + detail);
	}
	long status = 0;
	curl_easy_getinfo(easy.handle, CURLINFO_RESPONSE_CODE, &status);
	response.status = static_cast<int>(status);
	return response;
}
