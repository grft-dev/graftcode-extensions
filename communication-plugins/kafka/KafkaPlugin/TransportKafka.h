#pragma once
#include "ITransport.h"
#include "KafkaClient.h"
#include <memory>
#include <vector>

class TransportKafka : public Hypertube::Native::Interfaces::ITransport {
public:
    TransportKafka(const char* /*ip*/, unsigned short /*port*/, const char* configJson);
    int Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion) override;
    std::vector<byte> SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) override;

private:
    std::unique_ptr<KafkaClient> client_;
};
