#include "ServiceBusClient.h"

#include <azure/core/context.hpp>
#include <azure/core/datetime.hpp>
#include <azure/core/amqp/internal/connection.hpp>
#include <azure/core/amqp/internal/connection_string_credential.hpp>
#include <azure/core/amqp/internal/message_receiver.hpp>
#include <azure/core/amqp/internal/message_sender.hpp>
#include <azure/core/amqp/internal/models/message_source.hpp>
#include <azure/core/amqp/internal/session.hpp>
#include <azure/core/amqp/models/amqp_message.hpp>
#include <azure/core/amqp/models/amqp_value.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Graftcode::Plugins::ServiceBus;

namespace
{
	using Json = nlohmann::json;
	namespace Amqp = Azure::Core::Amqp::_internal;
	namespace AmqpModels = Azure::Core::Amqp::Models;

	// Service Bus standard messages are capped at 256 KB (1 MB on premium tiers).
	constexpr std::uint64_t MaxMessageSize = 1024ULL * 1024ULL;
	constexpr std::uint32_t DefaultRpcTimeoutMs = 30000;

	// AMQP source filter symbol used by Azure Service Bus to bind a receiver to a
	// specific session. Matches the value used by the official .NET/Java SDKs.
	constexpr const char* SessionFilterName = "com.microsoft:session-filter";

	std::atomic<std::uint64_t> correlationCounter_{ 0 };

	std::string BuildCorrelationId()
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto sequence = correlationCounter_.fetch_add(1, std::memory_order_relaxed);
		std::ostringstream oss;
		oss << "graftcode-" << std::this_thread::get_id() << "-" << now << "-" << sequence;
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

	const Json* FindServiceBusNode(const Json& node, bool isRoot = false)
	{
		if (node.is_object()) {
			const auto sbIt = node.find("servicebus");
			if (sbIt != node.end() && sbIt->is_object()) {
				return &(*sbIt);
			}

			const bool hasDedicatedKeys = node.contains("connectionString")
				|| node.contains("queue")
				|| node.contains("replyQueue")
				|| node.contains("sharedAccessKeyName")
				|| node.contains("sharedAccessKey");
			const bool isPluginChannel = node.contains("type")
				&& node["type"].is_string()
				&& node["type"].get<std::string>() == "plugin";

			if (hasDedicatedKeys || ((isPluginChannel || isRoot) && node.contains("connectionString"))) {
				return &node;
			}

			for (auto it = node.begin(); it != node.end(); ++it) {
				const Json* found = FindServiceBusNode(*it, false);
				if (found != nullptr) {
					return found;
				}
			}
			return nullptr;
		}

		if (node.is_array()) {
			for (const auto& item : node) {
				const Json* found = FindServiceBusNode(item, false);
				if (found != nullptr) {
					return found;
				}
			}
		}

		return nullptr;
	}

	void ApplyServiceBusConfig(const Json& node, ServiceBusClientConfig& config)
	{
		if (!node.is_object()) {
			return;
		}

		const auto getString = [&node](const char* key, std::string& target) {
			const auto it = node.find(key);
			if (it != node.end() && it->is_string()) {
				target = it->get<std::string>();
			}
			};

		getString("connectionString", config.connectionString);
		getString("host", config.host);
		getString("sharedAccessKeyName", config.sharedAccessKeyName);
		getString("sharedAccessKey", config.sharedAccessKey);
		getString("queue", config.queue);
		getString("replyQueue", config.replyQueue);
		getString("topic", config.topic);

		const auto emulatorIt = node.find("useDevelopmentEmulator");
		if (emulatorIt != node.end() && emulatorIt->is_boolean()) {
			config.useDevelopmentEmulator = emulatorIt->get<bool>();
		}

		const auto timeoutIt = node.find("rpcTimeoutMs");
		if (timeoutIt != node.end() && timeoutIt->is_number_integer()) {
			const auto parsedTimeout = timeoutIt->get<int64_t>();
			if (parsedTimeout > 0 && parsedTimeout <= std::numeric_limits<std::uint32_t>::max()) {
				config.rpcTimeoutMs = static_cast<std::uint32_t>(parsedTimeout);
			}
		}
	}

	std::vector<byte> ExtractMessageBody(const AmqpModels::AmqpMessage& message)
	{
		std::vector<byte> result;
		switch (message.BodyType) {
		case AmqpModels::MessageBodyType::Data: {
			for (const auto& section : message.GetBodyAsBinary()) {
				const auto* data = section.data();
				result.insert(result.end(), data, data + section.size());
			}
			break;
		}
		case AmqpModels::MessageBodyType::Value: {
			try {
				const auto valueAsString = static_cast<std::string>(message.GetBodyAsAmqpValue());
				result.assign(valueAsString.begin(), valueAsString.end());
			}
			catch (...) {
				// Non-string AMQP value bodies are not supported by the graftcode contract.
			}
			break;
		}
		default:
			break;
		}
		return result;
	}
}

std::string ServiceBusClient::BuildConnectionString(const ServiceBusClientConfig& config)
{
	if (!config.connectionString.empty()) {
		return config.connectionString;
	}

	if (config.host.empty() || config.sharedAccessKeyName.empty() || config.sharedAccessKey.empty()) {
		return std::string();
	}

	std::string host = config.host;
	const std::string scheme = "sb://";
	if (host.rfind(scheme, 0) == std::string::npos) {
		host = scheme + host;
	}
	if (!host.empty() && host.back() != '/') {
		host.push_back('/');
	}

	std::string connectionString = "Endpoint=" + host
		+ ";SharedAccessKeyName=" + config.sharedAccessKeyName
		+ ";SharedAccessKey=" + config.sharedAccessKey;
	if (config.useDevelopmentEmulator) {
		connectionString += ";UseDevelopmentEmulator=true";
	}
	return connectionString;
}

void ServiceBusClient::ApplyConfigSource(const std::string& configSource, ServiceBusClientConfig& config)
{
	const std::optional<Json> parsedSource = ParseJsonConfigSource(configSource);
	if (!parsedSource.has_value()) {
		return;
	}

	const Json* node = FindServiceBusNode(parsedSource.value(), true);
	if (node == nullptr) {
		return;
	}

	ApplyServiceBusConfig(*node, config);
}

bool ServiceBusClient::Send(const ServiceBusClientConfig& config, const std::vector<byte>& payload)
{
	std::vector<byte> responsePayload;
	return SendRpc(config, payload, responsePayload);
}

bool ServiceBusClient::Send(const ServiceBusClientConfig& config, const byte* payload, std::size_t payloadSize)
{
	std::vector<byte> responsePayload;
	return SendRpc(config, payload, payloadSize, responsePayload);
}

bool ServiceBusClient::PublishOneWay(const ServiceBusClientConfig& config, const std::vector<byte>& payload)
{
	const byte* data = payload.empty() ? nullptr : payload.data();
	return PublishOneWay(config, data, payload.size());
}

bool ServiceBusClient::PublishOneWay(
	const ServiceBusClientConfig& config,
	const byte* payload,
	std::size_t payloadSize)
{
	if (config.topic.empty()) {
		return false;
	}
	try {
		ServiceBusRpcClient client(config);
		client.Call(payload, payloadSize);
		return true;
	}
	catch (...) {
		return false;
	}
}

bool ServiceBusClient::SendRpc(
	const ServiceBusClientConfig& config,
	const std::vector<byte>& payload,
	std::vector<byte>& responsePayload)
{
	const byte* data = payload.empty() ? nullptr : payload.data();
	return SendRpc(config, data, payload.size(), responsePayload);
}

bool ServiceBusClient::SendRpc(
	const ServiceBusClientConfig& config,
	const byte* payload,
	std::size_t payloadSize,
	std::vector<byte>& responsePayload)
{
	responsePayload.clear();
	try {
		ServiceBusRpcClient client(config);
		responsePayload = client.Call(payload, payloadSize);
		return true;
	}
	catch (...) {
		responsePayload.clear();
		return false;
	}
}

// One AMQP connection with a request sender and, in RPC mode, a reply receiver bound to the
// client's reply session. A background thread dispatches replies by correlation id.
class ServiceBusRpcClient::Link
{
public:
	Link(const ServiceBusClientConfig& config, const std::string& replySessionId, Concurrency::PendingReplies& pending)
		: pending_(pending)
	{
		const std::string connectionString = ServiceBusClient::BuildConnectionString(config);
		if (connectionString.empty()) {
			throw std::runtime_error("Service Bus connection string is not configured");
		}
		const bool oneWay = !config.topic.empty();
		if (!oneWay && (config.queue.empty() || config.replyQueue.empty())) {
			throw std::runtime_error("Service Bus queue and replyQueue must be configured");
		}

		auto credential = std::make_shared<Amqp::ServiceBusSasConnectionStringCredential>(connectionString);
		Amqp::ConnectionOptions connectionOptions;
		connectionOptions.ContainerId = "graftcode-servicebus-client";
		connectionOptions.EnableTrace = false;
		const std::uint16_t credentialPort = credential->GetPort();
		connectionOptions.Port = credentialPort != 0 ? credentialPort : Amqp::AmqpTlsPort;
		connection_ = std::make_unique<Amqp::Connection>(credential->GetHostName(), credential, connectionOptions);

		Amqp::SessionOptions sessionOptions;
		sessionOptions.InitialIncomingWindowSize = (std::numeric_limits<std::int32_t>::max)();
		sessionOptions.InitialOutgoingWindowSize = (std::numeric_limits<std::uint16_t>::max)();
		session_ = std::make_unique<Amqp::Session>(connection_->CreateSession(sessionOptions));

		if (!oneWay) {
			// The reply queue is session enabled; the server stamps the reply session id on every
			// response, so this receiver sees exactly the replies addressed to this client.
			AmqpModels::_internal::MessageSourceOptions replySourceOptions;
			replySourceOptions.Address = AmqpModels::AmqpValue(config.replyQueue);
			AmqpModels::AmqpMap sessionFilter;
			sessionFilter[AmqpModels::AmqpSymbol(SessionFilterName).AsAmqpValue()]
				= AmqpModels::AmqpValue(replySessionId);
			replySourceOptions.Filter = sessionFilter;
			AmqpModels::_internal::MessageSource replySource(replySourceOptions);

			Amqp::MessageReceiverOptions receiverOptions;
			receiverOptions.Name = "graftcode-rpc-reply";
			receiverOptions.MessageTarget = "graftcode-rpc-reply-target";
			receiverOptions.SettleMode = Amqp::ReceiverSettleMode::First;
			receiverOptions.MaxMessageSize = MaxMessageSize;
			receiverOptions.MaxLinkCredit = 100;
			receiver_ = std::make_unique<Amqp::MessageReceiver>(
				session_->CreateMessageReceiver(replySource, receiverOptions, nullptr));
			receiver_->Open();
		}

		Amqp::MessageSenderOptions senderOptions;
		senderOptions.Name = oneWay ? "graftcode-oneway-publish" : "graftcode-rpc-request";
		senderOptions.MessageSource = oneWay ? "graftcode-oneway-publish-source" : "graftcode-rpc-request-source";
		senderOptions.SettleMode = Amqp::SenderSettleMode::Settled;
		senderOptions.MaxMessageSize = MaxMessageSize;
		sender_ = std::make_unique<Amqp::MessageSender>(
			session_->CreateMessageSender(oneWay ? config.topic : config.queue, senderOptions, nullptr));
		if (auto openError = sender_->Open()) {
			(void)openError;
			if (receiver_) {
				receiver_->Close();
			}
			throw std::runtime_error("failed to open Service Bus sender");
		}

		if (receiver_) {
			dispatcher_ = std::thread(&Link::DispatchReplies, this);
		}
	}

	~Link()
	{
		stopping_.store(true, std::memory_order_release);
		if (dispatcher_.joinable()) {
			dispatcher_.join();
		}
		try {
			if (receiver_) {
				receiver_->Close();
			}
			sender_->Close();
		}
		catch (...) {
		}
	}

	Link(const Link&) = delete;
	Link& operator=(const Link&) = delete;

	bool Broken() const
	{
		return broken_.load(std::memory_order_acquire);
	}

	void Send(const AmqpModels::AmqpMessage& message)
	{
		bool sendSucceeded = false;
		{
			std::lock_guard<std::mutex> lock(sendMutex_);
#if defined(ENABLE_RUST_AMQP) && ENABLE_RUST_AMQP
			sendSucceeded = !static_cast<bool>(sender_->Send(message));
#else
			const auto sendResult = sender_->Send(message);
			sendSucceeded = std::get<0>(sendResult) == Amqp::MessageSendStatus::Ok;
#endif
		}
		if (!sendSucceeded) {
			broken_.store(true, std::memory_order_release);
			throw std::runtime_error("failed to send Service Bus message");
		}
	}

private:
	void DispatchReplies()
	{
		while (!stopping_.load(std::memory_order_acquire)) {
			const auto deadline = std::chrono::system_clock::now() + ReceivePollInterval;
			Azure::Core::Context context = Azure::Core::Context{}.WithDeadline(Azure::DateTime(deadline));
			std::pair<std::shared_ptr<const AmqpModels::AmqpMessage>, AmqpModels::_internal::AmqpError> incoming;
			try {
				incoming = receiver_->WaitForIncomingMessage(context);
			}
			catch (const Azure::Core::OperationCancelledException&) {
				continue;
			}
			catch (const std::exception& error) {
				Disconnect(std::string("Service Bus reply receiver failed: ") + error.what());
				return;
			}
			if (incoming.second) {
				Disconnect("Service Bus reply receiver failed: " + incoming.second.Description);
				return;
			}
			if (!incoming.first) {
				continue;
			}

			const auto& properties = incoming.first->Properties;
			const auto& correlation = properties.CorrelationId.IsNull() ? properties.MessageId : properties.CorrelationId;
			if (correlation.IsNull()) {
				continue;
			}
			try {
				pending_.Complete(static_cast<std::string>(correlation), ExtractMessageBody(*incoming.first));
			}
			catch (...) {
			}
		}
	}

	void Disconnect(const std::string& error)
	{
		broken_.store(true, std::memory_order_release);
		pending_.FailAll(error);
	}

	static constexpr std::chrono::milliseconds ReceivePollInterval{ 500 };

	Concurrency::PendingReplies& pending_;
	std::unique_ptr<Amqp::Connection> connection_;
	std::unique_ptr<Amqp::Session> session_;
	std::unique_ptr<Amqp::MessageReceiver> receiver_;
	std::unique_ptr<Amqp::MessageSender> sender_;
	std::mutex sendMutex_;
	std::thread dispatcher_;
	std::atomic_bool stopping_{ false };
	std::atomic_bool broken_{ false };
};

ServiceBusRpcClient::ServiceBusRpcClient(ServiceBusClientConfig config)
	: config_(std::move(config)), replySessionId_(BuildCorrelationId())
{
}

ServiceBusRpcClient::~ServiceBusRpcClient()
{
	pending_.Close("Service Bus client is shutting down");
	std::lock_guard<std::mutex> lock(linkMutex_);
	link_.reset();
}

std::shared_ptr<ServiceBusRpcClient::Link> ServiceBusRpcClient::CurrentLink()
{
	std::lock_guard<std::mutex> lock(linkMutex_);
	if (!link_ || link_->Broken()) {
		link_.reset();
		link_ = std::make_shared<Link>(config_, replySessionId_, pending_);
	}
	return link_;
}

std::vector<byte> ServiceBusRpcClient::Call(const byte* payload, std::size_t payloadSize)
{
	if (payloadSize > 0 && payload == nullptr) {
		throw std::invalid_argument("Service Bus client: null payload with non-zero length");
	}

	const std::shared_ptr<Link> link = CurrentLink();
	const bool oneWay = !config_.topic.empty();
	const std::string correlationId = BuildCorrelationId();

	AmqpModels::AmqpMessage message;
	std::vector<std::uint8_t> body;
	if (payloadSize > 0) {
		body.assign(payload, payload + payloadSize);
	}
	message.SetBody(AmqpModels::AmqpBinaryData(body));
	message.Properties.MessageId = AmqpModels::AmqpValue(correlationId);
	message.Properties.ContentType = std::string("application/octet-stream");

	if (oneWay) {
		link->Send(message);
		return {};
	}

	message.Properties.ReplyTo = AmqpModels::AmqpValue(config_.replyQueue);
	// ReplyToGroupId maps to the Service Bus ReplyToSessionId: the server stamps it on the response.
	message.Properties.ReplyToGroupId = replySessionId_;

	auto ticket = pending_.Register(correlationId);
	link->Send(message);

	const std::uint32_t timeoutMs = config_.rpcTimeoutMs == 0 ? DefaultRpcTimeoutMs : config_.rpcTimeoutMs;
	std::vector<byte> response;
	if (!ticket.Wait(std::chrono::milliseconds(timeoutMs), response)) {
		throw std::runtime_error("Service Bus RPC timed out after " + std::to_string(timeoutMs)
			+ " ms waiting for correlation id " + correlationId);
	}
	return response;
}
