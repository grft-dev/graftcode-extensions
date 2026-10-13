#pragma once
#include "ITransport.h"
#include "KafkaClient.h"
#include <map>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <vector>

class TransportKafka : public Hypertube::Native::Interfaces::ITransport {
public:
    TransportKafka(const char* /*ip*/, unsigned short /*port*/, const char* configJson);
    int Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion) override;
    int SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) override;
    int ReadResponse(byte* responseByteArray, int32_t responseByteArrayLen) override;

private:
    std::unique_ptr<KafkaClient> client_;
    std::map<std::thread::id, std::vector<unsigned char>> responseByThread_;
    std::shared_mutex responseByThreadMutex_;
};
