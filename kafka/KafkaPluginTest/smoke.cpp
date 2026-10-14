#include "IServer.h"
#include "TransportPluginAbi.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define KAFKA_PLUGIN_IMPORT extern "C" __declspec(dllimport)
#else
#define KAFKA_PLUGIN_IMPORT extern "C"
#endif

KAFKA_PLUGIN_IMPORT GraftcodeGateway::IServer* CreateServer();
KAFKA_PLUGIN_IMPORT void DestroyServer(GraftcodeGateway::IServer* server);
KAFKA_PLUGIN_IMPORT GraftcodeTransportChannel* CreateTransport(
    const char* ipAddress, unsigned short port, const char* configSource, char** errorMessage);
KAFKA_PLUGIN_IMPORT void DestroyTransport(GraftcodeTransportChannel* channel);
KAFKA_PLUGIN_IMPORT void TransportFreeBuffer(void* buffer);

static bool g_processCalled = false;

static bool smokeProcess(const GraftcodeGateway::IServer::byte* /*requestData*/,
                         std::size_t /*requestSize*/,
                         GraftcodeGateway::IServer::WriteResponseFn writeResponse,
                         void* writeContext) {
    g_processCalled = true;
    static const unsigned char kPayload[] = {'o', 'k'};
    if (writeResponse) {
        writeResponse(writeContext, kPayload, sizeof(kPayload));
    }
    return true;
}

int main() {
    // Link / symbol smoke — no broker required.
    GraftcodeGateway::IServer* server = CreateServer();
    if (!server) {
        std::puts("FAIL: CreateServer returned null");
        return 1;
    }

    const char* cfg =
        R"({"brokers":"127.0.0.1:1","requestTopic":"graft.requests","replyTopic":"graft.replies","groupId":"graft-smoke"})";
    server->configure(cfg, smokeProcess);
    // Do not start() — that would spin a reconnect loop against a missing broker.
    DestroyServer(server);

    char* errorMessage = nullptr;
    GraftcodeTransportChannel* transport =
        CreateTransport("127.0.0.1", 0, cfg, &errorMessage);
    if (transport == nullptr) {
        std::printf("FAIL: CreateTransport returned null: %s\n", errorMessage != nullptr ? errorMessage : "");
        TransportFreeBuffer(errorMessage);
        return 1;
    }
    DestroyTransport(transport);

    std::puts("KafkaPlugin smoke OK — factories linked; run against local Kafka to exercise RPC");
    return 0;
}
