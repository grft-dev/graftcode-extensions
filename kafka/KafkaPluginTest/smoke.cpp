#include "IServer.h"
#include "ITransport.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define KAFKA_PLUGIN_IMPORT extern "C" __declspec(dllimport)
#else
#define KAFKA_PLUGIN_IMPORT extern "C"
#endif

KAFKA_PLUGIN_IMPORT GraftcodeGateway::IServer* CreateServer();
KAFKA_PLUGIN_IMPORT void DestroyServer(GraftcodeGateway::IServer* server);
KAFKA_PLUGIN_IMPORT Hypertube::Native::Interfaces::ITransport* CreateTransportChannel(
    const char* ipAddress, unsigned short port, const char* configSource);
KAFKA_PLUGIN_IMPORT void DestroyTransportChannel(Hypertube::Native::Interfaces::ITransport* transport);

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

    Hypertube::Native::Interfaces::ITransport* transport =
        CreateTransportChannel("127.0.0.1", 0, cfg);
    if (!transport) {
        std::puts("FAIL: CreateTransportChannel returned null");
        return 1;
    }
    if (transport->Initialize(1, 2, 1) != 0) {
        std::puts("FAIL: Initialize returned non-zero");
        DestroyTransportChannel(transport);
        return 1;
    }
    DestroyTransportChannel(transport);

    std::puts("KafkaPlugin smoke OK — factories linked; run against local Kafka to exercise RPC");
    return 0;
}
