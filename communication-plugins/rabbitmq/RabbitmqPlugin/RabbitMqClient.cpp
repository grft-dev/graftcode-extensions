#include "RabbitMqClient.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <amqpcpp.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace Graftcode::Plugins::Rabbitmq;

namespace
{
	using Json = nlohmann::json;

#ifdef _WIN32
	using SocketHandle = SOCKET;
	constexpr SocketHandle InvalidSocket = INVALID_SOCKET;
#else
	using SocketHandle = int;
	constexpr SocketHandle InvalidSocket = -1;
#endif

	constexpr int ReadBufferSize = 64 * 1024;
	constexpr std::chrono::seconds ConnectionReadyTimeout(5);

	void CloseSocket(SocketHandle socket)
	{
		if (socket == InvalidSocket) {
			return;
		}
#ifdef _WIN32
		closesocket(socket);
#else
		::shutdown(socket, SHUT_RDWR);
		::close(socket);
#endif
	}

	bool IsWouldBlockError()
	{
#ifdef _WIN32
		const int err = WSAGetLastError();
		return err == WSAEWOULDBLOCK || err == WSAEINTR;
#else
		return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
#endif
	}

	SocketHandle ConnectSocket(const RabbitMqClientConfig& config)
	{
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo* results = nullptr;
		const std::string portAsString = std::to_string(config.port);
		const int gaiResult = getaddrinfo(config.host.c_str(), portAsString.c_str(), &hints, &results);
		if (gaiResult != 0) {
			return InvalidSocket;
		}

		SocketHandle socket = InvalidSocket;
		for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
			socket = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (socket == InvalidSocket) {
				continue;
			}

			if (::connect(socket, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
				break;
			}

			CloseSocket(socket);
			socket = InvalidSocket;
		}

		freeaddrinfo(results);
		return socket;
	}

	enum class WaitStatus
	{
		Readable,
		Timeout,
		Error
	};

	WaitStatus WaitForSocketReadable(SocketHandle socket, int timeoutMs)
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socket, &readSet);

		timeval timeout{};
		timeout.tv_sec = timeoutMs / 1000;
		timeout.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
		const int result = ::select(0, &readSet, nullptr, nullptr, &timeout);
#else
		const int result = ::select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
		if (result > 0 && FD_ISSET(socket, &readSet)) {
			return WaitStatus::Readable;
		}
		if (result == 0) {
			return WaitStatus::Timeout;
		}
		return WaitStatus::Error;
	}

	struct RuntimeState
	{
		std::atomic_bool connectionReady{ false };
		std::atomic_bool connectionClosed{ false };
		std::atomic_bool connectionError{ false };
	};

	class SocketConnectionHandler final : public AMQP::ConnectionHandler
	{
	public:
		SocketConnectionHandler(SocketHandle socket, RuntimeState& state)
			: socket_(socket), state_(state)
		{
		}

		void onData(AMQP::Connection* connection, const char* buffer, size_t size) override
		{
			(void)connection;
			if (size == 0 || buffer == nullptr) {
				return;
			}

			std::lock_guard<std::mutex> lock(sendMutex_);
			size_t sentTotal = 0;
			while (sentTotal < size) {
#ifdef _WIN32
				const int sent = ::send(socket_, buffer + sentTotal, static_cast<int>(size - sentTotal), 0);
#else
				const ssize_t sent = ::send(socket_, buffer + sentTotal, size - sentTotal, 0);
#endif
				if (sent <= 0) {
					state_.connectionError.store(true, std::memory_order_release);
					return;
				}
				sentTotal += static_cast<size_t>(sent);
			}
		}

		void onReady(AMQP::Connection* connection) override
		{
			(void)connection;
			state_.connectionReady.store(true, std::memory_order_release);
		}

		void onError(AMQP::Connection* connection, const char* message) override
		{
			(void)connection;
			(void)message;
			state_.connectionError.store(true, std::memory_order_release);
		}

		void onClosed(AMQP::Connection* connection) override
		{
			(void)connection;
			state_.connectionClosed.store(true, std::memory_order_release);
		}

	private:
		SocketHandle socket_;
		RuntimeState& state_;
		std::mutex sendMutex_;
	};

	std::atomic<std::uint64_t> correlationCounter{ 0 };

	std::string BuildCorrelationId()
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto sequence = correlationCounter.fetch_add(1, std::memory_order_relaxed);
		std::ostringstream oss;
		oss << std::this_thread::get_id() << "-" << now << "-" << sequence;
		return oss.str();
	}

	std::optional<Json> ParseJsonConfigSource(const std::string& configSource)
	{
		if (configSource.empty()) {
			return std::nullopt;
		}

		try {
			return Json::parse(configSource);
		}
		catch (...) {
			// Treat source as a path to a json file.
		}

		try {
			const std::filesystem::path sourcePath(configSource);
			if (!std::filesystem::exists(sourcePath)) {
				return std::nullopt;
			}

			std::ifstream file(sourcePath);
			if (!file.good()) {
				return std::nullopt;
			}

			std::stringstream buffer;
			buffer << file.rdbuf();
			return Json::parse(buffer.str());
		}
		catch (...) {
			return std::nullopt;
		}
	}

	const Json* FindRabbitMqNode(const Json& node, bool isRoot = false)
	{
		if (node.is_object()) {
			const auto rabbitIt = node.find("rabbitmq");
			if (rabbitIt != node.end() && rabbitIt->is_object()) {
				return &(*rabbitIt);
			}

			const bool hasDedicatedRabbitKeys = node.contains("queue")
				|| node.contains("user")
				|| node.contains("password")
				|| node.contains("vhost")
				|| node.contains("rpcTimeoutMs");
			const bool hasHostAndPort = node.contains("host") && node.contains("port");
			const bool isPluginChannel = node.contains("type")
				&& node["type"].is_string()
				&& node["type"].get<std::string>() == "plugin";

			if (hasDedicatedRabbitKeys || (hasHostAndPort && (isPluginChannel || isRoot))) {
				return &node;
			}

			for (auto it = node.begin(); it != node.end(); ++it) {
				const Json* rabbitNode = FindRabbitMqNode(*it, false);
				if (rabbitNode != nullptr) {
					return rabbitNode;
				}
			}
			return nullptr;
		}

		if (node.is_array()) {
			for (const auto& item : node) {
				const Json* rabbitNode = FindRabbitMqNode(item, false);
				if (rabbitNode != nullptr) {
					return rabbitNode;
				}
			}
		}

		return nullptr;
	}

	void ApplyRabbitMqConfig(const Json& rabbitNode, RabbitMqClientConfig& config)
	{
		if (!rabbitNode.is_object()) {
			return;
		}

		const auto hostIt = rabbitNode.find("host");
		if (hostIt != rabbitNode.end() && hostIt->is_string()) {
			config.host = hostIt->get<std::string>();
		}

		const auto portIt = rabbitNode.find("port");
		if (portIt != rabbitNode.end() && portIt->is_number_integer()) {
			const auto parsedPort = portIt->get<int64_t>();
			if (parsedPort > 0 && parsedPort <= std::numeric_limits<std::uint16_t>::max()) {
				config.port = static_cast<std::uint16_t>(parsedPort);
			}
		}

		const auto queueIt = rabbitNode.find("queue");
		if (queueIt != rabbitNode.end() && queueIt->is_string()) {
			config.queue = queueIt->get<std::string>();
		}

		const auto userIt = rabbitNode.find("user");
		if (userIt != rabbitNode.end() && userIt->is_string()) {
			config.user = userIt->get<std::string>();
		}

		const auto passwordIt = rabbitNode.find("password");
		if (passwordIt != rabbitNode.end() && passwordIt->is_string()) {
			config.password = passwordIt->get<std::string>();
		}

		const auto vhostIt = rabbitNode.find("vhost");
		if (vhostIt != rabbitNode.end() && vhostIt->is_string()) {
			config.vhost = vhostIt->get<std::string>();
		}

		const auto timeoutIt = rabbitNode.find("rpcTimeoutMs");
		if (timeoutIt != rabbitNode.end() && timeoutIt->is_number_integer()) {
			const auto parsedTimeout = timeoutIt->get<int64_t>();
			if (parsedTimeout > 0 && parsedTimeout <= std::numeric_limits<std::uint32_t>::max()) {
				config.rpcTimeoutMs = static_cast<std::uint32_t>(parsedTimeout);
			}
		}
	}
}

void RabbitMqClient::ApplyConfigSource(const std::string& configSource, RabbitMqClientConfig& config)
{
	const std::optional<Json> parsedSource = ParseJsonConfigSource(configSource);
	if (!parsedSource.has_value()) {
		return;
	}

	const Json* rabbitNode = FindRabbitMqNode(parsedSource.value(), true);
	if (rabbitNode == nullptr) {
		return;
	}

	ApplyRabbitMqConfig(*rabbitNode, config);
}

bool RabbitMqClient::Send(const RabbitMqClientConfig& config, const std::vector<byte>& payload)
{
	std::vector<byte> responsePayload;
	return SendRpc(config, payload, responsePayload);
}

bool RabbitMqClient::Send(const RabbitMqClientConfig& config, const byte* payload, std::size_t payloadSize)
{
	std::vector<byte> responsePayload;
	return SendRpc(config, payload, payloadSize, responsePayload);
}

bool RabbitMqClient::SendRpc(
	const RabbitMqClientConfig& config,
	const std::vector<byte>& payload,
	std::vector<byte>& responsePayload)
{
	const byte* data = payload.empty() ? nullptr : payload.data();
	return SendRpc(config, data, payload.size(), responsePayload);
}

bool RabbitMqClient::SendRpc(
	const RabbitMqClientConfig& config,
	const byte* payload,
	std::size_t payloadSize,
	std::vector<byte>& responsePayload)
{
	responsePayload.clear();
	try {
		RabbitMqRpcClient client(config);
		responsePayload = client.Call(payload, payloadSize);
		return true;
	}
	catch (...) {
		responsePayload.clear();
		return false;
	}
}

// One AMQP connection and channel with a private, exclusive reply queue. AMQP-CPP is not
// thread safe, so every call into the connection or channel happens under amqpMutex_; the
// I/O thread waits for socket data without holding it and dispatches replies by correlation id.
class RabbitMqRpcClient::Link
{
public:
	Link(const RabbitMqClientConfig& config, Concurrency::PendingReplies& pending)
		: pending_(pending), queue_(config.queue)
	{
#ifdef _WIN32
		WSAData wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			throw std::runtime_error("RabbitMQ: WSAStartup failed");
		}
#endif
		socket_ = ConnectSocket(config);
		if (socket_ == InvalidSocket) {
			Shutdown();
			throw std::runtime_error(
				"RabbitMQ: cannot connect to " + config.host + ":" + std::to_string(config.port));
		}

		handler_ = std::make_unique<SocketConnectionHandler>(socket_, runtimeState_);
		connection_ = std::make_unique<AMQP::Connection>(
			handler_.get(),
			AMQP::Login(config.user, config.password),
			config.vhost.empty() ? "/" : config.vhost);
		channel_ = std::make_unique<AMQP::Channel>(connection_.get());
		channel_->onError([this](const char* message) {
			Disconnect(std::string("RabbitMQ channel error: ") + (message != nullptr ? message : ""));
			});
		channel_->declareQueue(AMQP::exclusive | AMQP::autodelete)
			.onSuccess([this](const std::string& name, uint32_t, uint32_t) {
				StartConsumer(name);
				});

		io_ = std::thread(&Link::Run, this);

		const auto deadline = std::chrono::steady_clock::now() + ConnectionReadyTimeout;
		while (!consumerReady_.load(std::memory_order_acquire) &&
			!broken_.load(std::memory_order_acquire) &&
			std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!consumerReady_.load(std::memory_order_acquire)) {
			const std::string error = broken_.load(std::memory_order_acquire)
				? BrokenError()
				: std::string("RabbitMQ: timed out waiting for the reply consumer");
			Shutdown();
			throw std::runtime_error(error);
		}
	}

	~Link()
	{
		Shutdown();
	}

	Link(const Link&) = delete;
	Link& operator=(const Link&) = delete;

	bool Broken() const
	{
		return broken_.load(std::memory_order_acquire);
	}

	void Publish(const std::string& correlationId, const byte* payload, std::size_t payloadSize)
	{
		std::lock_guard<std::mutex> lock(amqpMutex_);
		if (broken_.load(std::memory_order_acquire)) {
			throw std::runtime_error(BrokenError());
		}
		const char* body = payloadSize == 0 ? "" : reinterpret_cast<const char*>(payload);
		AMQP::Envelope envelope(body, payloadSize);
		envelope.setContentType("application/octet-stream");
		envelope.setReplyTo(replyQueue_);
		envelope.setCorrelationID(correlationId);
		if (!channel_->publish("", queue_, envelope)) {
			throw std::runtime_error("RabbitMQ: failed to publish request to queue " + queue_);
		}
	}

private:
	void StartConsumer(const std::string& replyQueue)
	{
		replyQueue_ = replyQueue;
		channel_->consume(replyQueue_)
			.onSuccess([this](const std::string&) {
				consumerReady_.store(true, std::memory_order_release);
				})
			.onReceived([this](const AMQP::Message& message, uint64_t deliveryTag, bool) {
				std::vector<byte> body(message.bodySize());
				if (!body.empty()) {
					std::memcpy(body.data(), message.body(), body.size());
				}
				channel_->ack(deliveryTag);
				if (message.hasCorrelationID()) {
					pending_.Complete(message.correlationID(), std::move(body));
				}
				});
	}

	void Run()
	{
		std::vector<char> receiveBuffer(ReadBufferSize);
		std::vector<char> pendingBytes;
		pendingBytes.reserve(ReadBufferSize);

		while (!stopping_.load(std::memory_order_acquire) && !broken_.load(std::memory_order_acquire)) {
			if (runtimeState_.connectionError.load(std::memory_order_acquire) ||
				runtimeState_.connectionClosed.load(std::memory_order_acquire)) {
				Disconnect("RabbitMQ: connection lost");
				return;
			}

			const WaitStatus waitStatus = WaitForSocketReadable(socket_, 200);
			if (waitStatus == WaitStatus::Timeout) {
				continue;
			}
			if (waitStatus == WaitStatus::Error) {
				Disconnect("RabbitMQ: socket error");
				return;
			}

#ifdef _WIN32
			const int bytesRead = ::recv(socket_, receiveBuffer.data(), static_cast<int>(receiveBuffer.size()), 0);
#else
			const ssize_t bytesRead = ::recv(socket_, receiveBuffer.data(), receiveBuffer.size(), 0);
#endif
			if (bytesRead == 0) {
				Disconnect("RabbitMQ: connection closed by broker");
				return;
			}
			if (bytesRead < 0) {
				if (IsWouldBlockError()) {
					continue;
				}
				Disconnect("RabbitMQ: socket read failed");
				return;
			}

			pendingBytes.insert(
				pendingBytes.end(),
				receiveBuffer.begin(),
				receiveBuffer.begin() + static_cast<std::ptrdiff_t>(bytesRead));

			std::lock_guard<std::mutex> lock(amqpMutex_);
			while (!pendingBytes.empty()) {
				const std::uint64_t parsed = connection_->parse(pendingBytes.data(), pendingBytes.size());
				if (parsed == 0 || parsed > pendingBytes.size()) {
					break;
				}
				pendingBytes.erase(
					pendingBytes.begin(),
					pendingBytes.begin() + static_cast<std::ptrdiff_t>(parsed));
			}
		}
	}

	void Disconnect(const std::string& error)
	{
		{
			std::lock_guard<std::mutex> lock(errorMutex_);
			if (brokenError_.empty()) {
				brokenError_ = error;
			}
		}
		broken_.store(true, std::memory_order_release);
		pending_.FailAll(error);
	}

	std::string BrokenError() const
	{
		std::lock_guard<std::mutex> lock(errorMutex_);
		return brokenError_.empty() ? std::string("RabbitMQ: connection is not available") : brokenError_;
	}

	void Shutdown()
	{
		stopping_.store(true, std::memory_order_release);
		if (io_.joinable()) {
			io_.join();
		}
		if (connection_) {
			connection_->close();
		}
		channel_.reset();
		connection_.reset();
		handler_.reset();
		if (socket_ != InvalidSocket) {
			CloseSocket(socket_);
			socket_ = InvalidSocket;
		}
#ifdef _WIN32
		if (!wsaCleaned_) {
			wsaCleaned_ = true;
			WSACleanup();
		}
#endif
	}

	Concurrency::PendingReplies& pending_;
	std::string queue_;
	std::string replyQueue_;
	SocketHandle socket_{ InvalidSocket };
	RuntimeState runtimeState_{};
	std::unique_ptr<SocketConnectionHandler> handler_;
	std::unique_ptr<AMQP::Connection> connection_;
	std::unique_ptr<AMQP::Channel> channel_;
	std::mutex amqpMutex_;
	mutable std::mutex errorMutex_;
	std::string brokenError_;
	std::thread io_;
	std::atomic_bool consumerReady_{ false };
	std::atomic_bool broken_{ false };
	std::atomic_bool stopping_{ false };
#ifdef _WIN32
	bool wsaCleaned_{ false };
#endif
};

RabbitMqRpcClient::RabbitMqRpcClient(RabbitMqClientConfig config)
	: config_(std::move(config))
{
}

RabbitMqRpcClient::~RabbitMqRpcClient()
{
	pending_.Close("RabbitMQ client is shutting down");
	std::lock_guard<std::mutex> lock(linkMutex_);
	link_.reset();
}

std::shared_ptr<RabbitMqRpcClient::Link> RabbitMqRpcClient::CurrentLink()
{
	std::lock_guard<std::mutex> lock(linkMutex_);
	if (!link_ || link_->Broken()) {
		link_.reset();
		link_ = std::make_shared<Link>(config_, pending_);
	}
	return link_;
}

std::vector<byte> RabbitMqRpcClient::Call(const byte* payload, std::size_t payloadSize)
{
	if (config_.queue.empty()) {
		throw std::runtime_error("RabbitMQ queue is not configured");
	}
	if (payloadSize > 0 && payload == nullptr) {
		throw std::invalid_argument("RabbitMQ client: null payload with non-zero length");
	}

	const std::shared_ptr<Link> link = CurrentLink();
	const std::string correlationId = BuildCorrelationId();
	auto ticket = pending_.Register(correlationId);
	link->Publish(correlationId, payload, payloadSize);

	const std::uint32_t timeoutMs = config_.rpcTimeoutMs == 0 ? 30000u : config_.rpcTimeoutMs;
	std::vector<byte> response;
	if (!ticket.Wait(std::chrono::milliseconds(timeoutMs), response)) {
		throw std::runtime_error("RabbitMQ RPC timed out after " + std::to_string(timeoutMs)
			+ " ms waiting for correlation id " + correlationId);
	}
	return response;
}
