#pragma once

#include <string>
#include <vector>

namespace PaymentZevio {
	struct HttpResponse {
		int status{ 0 };
		std::string body;
	};

	/** Minimal HTTPS client on WinHTTP (ships with Windows; system TLS and certificate store). Thread-safe. */
	class WinHttpClient {
	public:
		/** baseUrl e.g. https://api.sandbox.zevio.io/api/v1 */
		WinHttpClient(const std::string& baseUrl, std::string bearerToken, int timeoutMs);
		~WinHttpClient();
		WinHttpClient(const WinHttpClient&) = delete;
		WinHttpClient& operator=(const WinHttpClient&) = delete;

		/** method GET/POST; path relative to base ("qr/abc"); body JSON or empty. Throws std::runtime_error on transport errors. */
		HttpResponse send(const std::string& method, const std::string& path, const std::string& body) const;

	private:
		void* session_{ nullptr };
		std::wstring host_;
		std::wstring basePath_;
		unsigned short port_{ 443 };
		bool secure_{ true };
		std::string bearerToken_;
		int timeoutMs_{ 30000 };
	};
}
