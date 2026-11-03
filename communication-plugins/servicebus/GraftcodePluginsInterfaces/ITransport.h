/**
 * @file ITransport.h
 * @brief Provides the interface for the Transport class.
 */

#pragma once
#include "_common.h"
#include <vector>

namespace Hypertube::Native::Interfaces
{
    /**
     * @class ITransport
     * @brief Provides the interface for the Transport class, which is responsible for choosing transport channel (in memory, tcp or other).
     */
    class ITransport
    {
    public:
        virtual ~ITransport() = default;

        /**
         * @brief Initializes the transport with the calling runtime and called runtime version.
         * @param callingRuntimeNumber The calling runtime number.
         * @param calledRuntimeNumber The called runtime number.
         * @param calledRuntimeVersion The called runtime version.
         * @return Status code of the operation.
         */
        virtual int Initialize(byte callingRuntimeNumber, byte calledRuntimeNumber, byte calledRuntimeVersion) = 0;

        /**
         * @brief Sends a byte array to the transport and returns its response.
         * @param messageByteArray The byte array to send.
         * @param messageByteArrayLen The length of the byte array.
         * @return The response byte array.
         */
        virtual std::vector<byte> SendCommand(byte* messageByteArray, int32_t messageByteArrayLen) = 0;

    };
}
