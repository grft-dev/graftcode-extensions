#include "IServer.h"
#include "ITransport.h"

#include <cstdio>

#if defined(_WIN32)
#define SQS_PLUGIN_IMPORT extern "C" __declspec(dllimport)
#else
#define SQS_PLUGIN_IMPORT extern "C"
#endif

SQS_PLUGIN_IMPORT GraftcodeGateway::IServer* CreateServer();
SQS_PLUGIN_IMPORT void DestroyServer(GraftcodeGateway::IServer* server);
SQS_PLUGIN_IMPORT Hypertube::Native::Interfaces::ITransport* CreateTransportChannel(
    const char* ipAddress,
    unsigned short port,
    const char* configSource);
SQS_PLUGIN_IMPORT void DestroyTransportChannel(
    Hypertube::Native::Interfaces::ITransport* transport);

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
        "region": "us-east-1",
        "endpointOverride": "http://127.0.0.1:9",
        "accessKeyId": "test",
        "secretAccessKey": "test",
        "requestQueueUrl": "http://127.0.0.1:9/000000000000/graft-requests",
        "replyQueueUrl": "http://127.0.0.1:9/000000000000/graft-replies",
        "verifySsl": false
    })";

    GraftcodeGateway::IServer* server = CreateServer();
    if (server == nullptr) {
        std::puts("FAIL: CreateServer returned null");
        return 1;
    }
    server->configure(config, smokeProcess);
    // Do not start(); that would open a receive loop.
    server->stop();
    DestroyServer(server);

    Hypertube::Native::Interfaces::ITransport* transport =
        CreateTransportChannel(nullptr, 0, config);
    if (transport == nullptr) {
        std::puts("FAIL: CreateTransportChannel returned null");
        return 1;
    }
    if (transport->Initialize(1, 2, 1) != 0) {
        std::puts("FAIL: Initialize returned non-zero");
        DestroyTransportChannel(transport);
        return 1;
    }
    DestroyTransportChannel(transport);

    std::puts("SqsPlugin smoke OK");
    return 0;
}
