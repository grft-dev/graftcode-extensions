#include "IServer.h"
#include "TransportPluginAbi.h"

#include <cstdio>

#if defined(_WIN32)
#define PUBSUB_PLUGIN_IMPORT extern "C" __declspec(dllimport)
#else
#define PUBSUB_PLUGIN_IMPORT extern "C"
#endif

PUBSUB_PLUGIN_IMPORT GraftcodeGateway::IServer* CreateServer();
PUBSUB_PLUGIN_IMPORT void DestroyServer(GraftcodeGateway::IServer* server);
PUBSUB_PLUGIN_IMPORT TransportPluginChannel* CreateTransport(
    const char* ipAddress, unsigned short port, const char* configSource, char** errorMessage);
PUBSUB_PLUGIN_IMPORT void DestroyTransport(TransportPluginChannel* channel);
PUBSUB_PLUGIN_IMPORT void TransportFreeBuffer(void* buffer);

static bool smokeProcess(
    const GraftcodeGateway::IServer::byte*,
    std::size_t,
    GraftcodeGateway::IServer::WriteResponseFn writeResponse,
    void* writeContext)
{
    static const unsigned char payload[] = { 'o', 'k' };
    if (writeResponse != nullptr) {
        writeResponse(writeContext, payload, sizeof(payload));
    }
    return true;
}

int main()
{
    const char* config = R"({
        "projectId": "graftcode-local",
        "emulatorHost": "127.0.0.1:9",
        "requestTopic": "graft-requests",
        "replyTopic": "graft-replies",
        "autoCreate": false,
        "verifySsl": false
    })";

    GraftcodeGateway::IServer* server = CreateServer();
    if (server == nullptr) {
        std::puts("FAIL: CreateServer returned null");
        return 1;
    }
    server->configure(config, smokeProcess);
    // Do not start(); that would open a pull loop.
    server->stop();
    DestroyServer(server);

    char* errorMessage = nullptr;
    TransportPluginChannel* transport =
        CreateTransport(nullptr, 0, config, &errorMessage);
    if (transport == nullptr) {
        std::printf("FAIL: CreateTransport returned null: %s\n", errorMessage != nullptr ? errorMessage : "");
        TransportFreeBuffer(errorMessage);
        return 1;
    }
    DestroyTransport(transport);

    std::puts("PubSubPlugin smoke OK");
    return 0;
}
