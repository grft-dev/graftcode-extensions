#include "WinHttpClient.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>

#include <stdexcept>

using namespace PaymentZevio;

namespace {
	std::wstring widen(const std::string& s) {
		if (s.empty()) return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
		return w;
	}
	std::runtime_error lastError(const char* what) {
		return std::runtime_error(std::string(what) + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")");
	}
	struct Handle {
		HINTERNET h{ nullptr };
		explicit Handle(HINTERNET v) : h(v) {}
		~Handle() { if (h) WinHttpCloseHandle(h); }
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
	};
}

WinHttpClient::WinHttpClient(const std::string& baseUrl, std::string bearerToken, int timeoutMs)
	: bearerToken_(std::move(bearerToken)), timeoutMs_(timeoutMs) {
	std::wstring url = widen(baseUrl);
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256]{}; wchar_t path[1024]{};
	parts.lpszHostName = host; parts.dwHostNameLength = 255;
	parts.lpszUrlPath = path; parts.dwUrlPathLength = 1023;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
		throw std::runtime_error("Invalid Zevio API URL: " + baseUrl);
	host_ = host;
	basePath_ = path;
	while (!basePath_.empty() && basePath_.back() == L'/') basePath_.pop_back();
	port_ = parts.nPort;
	secure_ = parts.nScheme == INTERNET_SCHEME_HTTPS;
	session_ = WinHttpOpen(L"graftcode-payment-zevio/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session_) throw lastError("WinHttpOpen");
	WinHttpSetTimeouts(session_, timeoutMs_, timeoutMs_, timeoutMs_, timeoutMs_);
}

WinHttpClient::~WinHttpClient() {
	if (session_) WinHttpCloseHandle(session_);
}

HttpResponse WinHttpClient::send(const std::string& method, const std::string& path, const std::string& body) const {
	Handle connection(WinHttpConnect(session_, host_.c_str(), port_, 0));
	if (!connection.h) throw lastError("WinHttpConnect");
	const std::wstring fullPath = basePath_ + L"/" + widen(path);
	Handle request(WinHttpOpenRequest(connection.h, widen(method).c_str(), fullPath.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0));
	if (!request.h) throw lastError("WinHttpOpenRequest");

	std::wstring headers = L"Accept: application/json\r\nAuthorization: Bearer " + widen(bearerToken_) + L"\r\n";
	if (!body.empty()) headers += L"Content-Type: application/json; charset=utf-8\r\n";

	if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1L),
		body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
		static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0))
		throw lastError("WinHttpSendRequest");
	if (!WinHttpReceiveResponse(request.h, nullptr))
		throw lastError("WinHttpReceiveResponse");

	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

	HttpResponse response;
	response.status = static_cast<int>(status);
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request.h, &available)) throw lastError("WinHttpQueryDataAvailable");
		if (available == 0) break;
		std::string chunk(available, '\0');
		DWORD read = 0;
		if (!WinHttpReadData(request.h, chunk.data(), available, &read)) throw lastError("WinHttpReadData");
		chunk.resize(read);
		response.body += chunk;
	}
	return response;
}
