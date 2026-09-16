#pragma once
#include "ITransport.h"
#include "KafkaClient.h"
#include <memory>
#include <vector>

class TransportKafka : public Hypertube::Native::Interfaces::ITransport {
public:
    TransportKafka(const char* /*ip*/, unsigned short /*port*/, const char* configJson);
    int Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion) override;
    int SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) override;
    int ReadResponse(byte* responseByteArray, int32_t responseByteArrayLen) override;

private:
    std::unique_ptr<KafkaClient> client_;
    std::vector<unsigned char> lastResponse_;
};
