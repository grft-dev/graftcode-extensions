#include "HttpClient.h"

#include <curl/curl.h>

#include <cctype>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#include <urlmon.h>
#include <objbase.h>
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
		}
		return false;
	}

	/** WinHTTP's default autologon policy sends the current user only to an intranet proxy. */
	bool proxyAllowsDefaultCredentials(const std::string& proxyUrl) {
		const auto scheme = proxyUrl.find("://");
		const std::size_t start = scheme == std::string::npos ? 0 : scheme + 3;
		const auto at = proxyUrl.find('@', start);
		const auto slash = proxyUrl.find('/', start);
		if (at != std::string::npos && (slash == std::string::npos || at < slash))
			return false;

		const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool ownCom = SUCCEEDED(init);
		IInternetSecurityManager* manager = nullptr;
		const HRESULT created = CoCreateInstance(CLSID_InternetSecurityManager, nullptr, CLSCTX_INPROC_SERVER,
			IID_IInternetSecurityManager, reinterpret_cast<void**>(&manager));
		DWORD zone = URLZONE_UNTRUSTED;
		HRESULT mapped = E_FAIL;
		if (SUCCEEDED(created) && manager)
			mapped = manager->MapUrlToZone(widen(proxyUrl).c_str(), &zone, 0);
		if (manager) manager->Release();
		if (ownCom) CoUninitialize();
		return SUCCEEDED(mapped) && (zone == URLZONE_INTRANET || zone == URLZONE_LOCAL_MACHINE);
	}

	bool curlSupportsSspi() {
		const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
		return info != nullptr && (info->features & CURL_VERSION_SSPI) != 0;
	}

	void applyProxyAuthentication(CURL* easy, const std::string& proxyUrl) {
		const bool useCurrentUser = !proxyUrl.empty() && curlSupportsSspi() && proxyAllowsDefaultCredentials(proxyUrl);
		curl_easy_setopt(easy, CURLOPT_PROXYAUTH,
			useCurrentUser ? static_cast<long>(CURLAUTH_NEGOTIATE | CURLAUTH_NTLM) : static_cast<long>(CURLAUTH_NONE));
		curl_easy_setopt(easy, CURLOPT_PROXYUSERPWD, useCurrentUser ? ":" : "");
	}

	/** WinHTTP proxy token -> libcurl proxy URL. A bare host:port is an HTTP proxy. */
	std::string curlProxyUrl(std::string value) {
		const auto scheme = value.find("://");
		if (scheme != std::string::npos) return value;
		return "http://" + value;
	}

	/** Scheme-specific entries win. Otherwise every unqualified proxy is kept, in order. */
	std::vector<std::string> proxiesFor(const wchar_t* list, const std::string& targetScheme) {
		const std::string text = narrow(list);
		std::vector<std::string> specific;
		std::vector<std::string> generic;
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
				generic.push_back(curlProxyUrl(token));
				continue;
			}
			const std::string value = token.substr(eq + 1);
			if (value.empty() || lower(token.substr(0, eq)) != targetScheme) continue;
			specific.push_back(curlProxyUrl(value));
		}
		return specific.empty() ? generic : specific;
	}

	std::vector<std::string> proxiesFromInfo(const WINHTTP_PROXY_INFO& info, const std::string& url) {
		if (info.dwAccessType != WINHTTP_ACCESS_TYPE_NAMED_PROXY || !info.lpszProxy || !*info.lpszProxy)
			return {};
		if (info.lpszProxyBypass && bypasses(hostOf(url), info.lpszProxyBypass))
			return {};
		return proxiesFor(info.lpszProxy, schemeOf(url));
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

	struct ProxyChoice {
		bool known{ false };
		std::vector<std::string> proxies;
	};

	/**
	 * known false: Windows did not answer; libcurl may use proxy environment variables.
	 * known true and empty: direct connection.
	 * otherwise: ordered proxy candidates. A later proxy is used only when connecting to an earlier one fails.
	 */
	ProxyChoice windowsProxyFor(const std::string& url) {
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
					ProxyChoice choice;
					choice.known = true;
					choice.proxies = proxiesFromInfo(info, url);
					if (info.lpszProxy) GlobalFree(info.lpszProxy);
					if (info.lpszProxyBypass) GlobalFree(info.lpszProxyBypass);
					return choice;
				}
			}
		}

		if (haveIe && ie.lpszProxy && *ie.lpszProxy) {
			ProxyChoice choice;
			choice.known = true;
			if (!(ie.lpszProxyBypass && bypasses(hostOf(url), ie.lpszProxyBypass)))
				choice.proxies = proxiesFor(ie.lpszProxy, schemeOf(url));
			return choice;
		}
		if (haveIe) {
			ProxyChoice choice;
			choice.known = true;
			return choice;
		}

		WINHTTP_PROXY_INFO def{};
		if (!WinHttpGetDefaultProxyConfiguration(&def))
			return {};
		ProxyChoice choice;
		choice.known = true;
		choice.proxies = proxiesFromInfo(def, url);
		if (def.lpszProxy) GlobalFree(def.lpszProxy);
		if (def.lpszProxyBypass) GlobalFree(def.lpszProxyBypass);
		return choice;
	}
#endif

	struct ProxyAttempt {
		bool overrideProxy{ false };
		std::string proxy;
	};

	int noteRequestSent(CURL*, curl_infotype type, char* data, std::size_t size, void* userdata) {
		const bool tunnelRequest = type == CURLINFO_HEADER_OUT && size >= 8
			&& std::char_traits<char>::compare(data, "CONNECT ", 8) == 0;
		if (!tunnelRequest && (type == CURLINFO_HEADER_OUT || type == CURLINFO_DATA_OUT))
			*static_cast<bool*>(userdata) = true;
		return 0;
	}

	/** Connection failures may try the next proxy. A POST that already left the process is not sent again. */
	bool mayTryNextProxy(const std::string& method, CURLcode code, bool requestSent) {
		const bool post = method != "GET" && method != "HEAD";
		if (post && requestSent) return false;
		switch (code) {
		case CURLE_COULDNT_RESOLVE_PROXY:
		case CURLE_COULDNT_CONNECT:
			return true;
		case CURLE_COULDNT_RESOLVE_HOST:
		case CURLE_OPERATION_TIMEDOUT:
		case CURLE_SSL_CONNECT_ERROR:
			return !requestSent;
		default:
			return false;
		}
	}
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
	curl_easy_setopt(easy.handle, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(easy.handle, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, writeBody);
	curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(easy.handle, CURLOPT_ERRORBUFFER, errorBuffer);
	if (method != "GET" && method != "HEAD") {
		curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDS, body.data());
		curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
	}

	std::vector<ProxyAttempt> attempts;
#if defined(_WIN32)
	const ProxyChoice choice = windowsProxyFor(url);
	if (!choice.known) {
		attempts.push_back({});
	}
	else if (choice.proxies.empty()) {
		ProxyAttempt direct;
		direct.overrideProxy = true;
		attempts.push_back(std::move(direct));
	}
	else {
		for (const std::string& proxy : choice.proxies) {
			ProxyAttempt attempt;
			attempt.overrideProxy = true;
			attempt.proxy = proxy;
			attempts.push_back(std::move(attempt));
		}
	}
#else
	attempts.push_back({});
#endif

	std::string authorization = "Authorization: Bearer " + bearerToken_;
	Slist headers;
	headers.list = curl_slist_append(headers.list, "Accept: application/json");
	headers.list = curl_slist_append(headers.list, authorization.c_str());
	if (!body.empty())
		headers.list = curl_slist_append(headers.list, "Content-Type: application/json; charset=utf-8");
	curl_easy_setopt(easy.handle, CURLOPT_HTTPHEADER, headers.list);

	bool requestSent = false;
	if (attempts.size() > 1) {
		curl_easy_setopt(easy.handle, CURLOPT_DEBUGFUNCTION, noteRequestSent);
		curl_easy_setopt(easy.handle, CURLOPT_DEBUGDATA, &requestSent);
		curl_easy_setopt(easy.handle, CURLOPT_VERBOSE, 1L);
	}

	// timeoutMs 0 is the WinHTTP value for no timeout. libcurl uses 0 the same way.
	const bool unlimited = timeoutMs_ <= 0;
	const auto deadline = unlimited
		? std::chrono::steady_clock::time_point{}
		: std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs_);
	CURLcode code = CURLE_OPERATION_TIMEDOUT;
	for (std::size_t i = 0; i < attempts.size(); ++i) {
		long timeout = 0;
		if (!unlimited) {
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now()).count();
			if (left <= 0) break;
			timeout = static_cast<long>(left);
		}
		response.body.clear();
		requestSent = false;
		errorBuffer[0] = '\0';
		curl_easy_setopt(easy.handle, CURLOPT_TIMEOUT_MS, timeout);
		curl_easy_setopt(easy.handle, CURLOPT_CONNECTTIMEOUT_MS, timeout);
		if (i > 0)
			curl_easy_setopt(easy.handle, CURLOPT_FRESH_CONNECT, 1L);
		if (attempts[i].overrideProxy) {
			curl_easy_setopt(easy.handle, CURLOPT_PROXY, attempts[i].proxy.c_str());
			curl_easy_setopt(easy.handle, CURLOPT_NOPROXY, "");
#if defined(_WIN32)
			applyProxyAuthentication(easy.handle, attempts[i].proxy);
#endif
		}
		code = curl_easy_perform(easy.handle);
		if (code == CURLE_OK) break;
		if (i + 1 >= attempts.size() || !mayTryNextProxy(method, code, requestSent))
			break;
	}
	if (code != CURLE_OK) {
		const char* detail = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(code);
		throw std::runtime_error(std::string("Zevio ") + method + " failed: " + detail);
	}
	long status = 0;
	curl_easy_getinfo(easy.handle, CURLINFO_RESPONSE_CODE, &status);
	response.status = static_cast<int>(status);
	return response;
}
