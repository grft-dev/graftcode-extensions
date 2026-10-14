#pragma once

#include "_common.h"
#include <vector>

namespace Hypertube::Native::Interfaces
{
    class ITransport
    {
    public:
        virtual ~ITransport() = default;
        virtual int Initialize(
            byte callingRuntimeNumber,
            byte calledRuntimeNumber,
            byte calledRuntimeVersion) = 0;
        virtual std::vector<byte> SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) = 0;
    };
}
