#pragma once

#include <string>

namespace PaymentZevio {
	struct HttpResponse {
		int status{ 0 };
		std::string body;
	};

	/**
	 * HTTPS client on libcurl (vcpkg, linked into the plugin).
	 * Each send() owns its curl handle, so concurrent calls do not queue on one lock.
	 * On Windows, each transfer uses the system proxy list (WinHTTP automatic proxy, including PAC).
	 * An intranet proxy may authenticate with the current user's Negotiate or NTLM credentials. An internet proxy does not.
	 * The next proxy is tried only when connecting to an earlier one fails, and a payment POST is not sent again once any of it may have left the process.
	 */
	class HttpClient {
	public:
		/** baseUrl e.g. https://api.sandbox.zevio.io/api/v1 */
		HttpClient(const std::string& baseUrl, std::string bearerToken, int timeoutMs);
		HttpClient(const HttpClient&) = delete;
		HttpClient& operator=(const HttpClient&) = delete;

		/** method GET/POST; path relative to base ("qr/abc"); body JSON or empty. Throws std::runtime_error on transport errors. */
		HttpResponse send(const std::string& method, const std::string& path, const std::string& body) const;

	private:
		std::string baseUrl_;
		std::string bearerToken_;
		int timeoutMs_{ 30000 };
	};
}
