/**
 * @file TransportPluginExports.h
 * @brief Implements the transport plugin C ABI (TransportPluginAbi.h) on top of ITransport.
 */

#pragma once
#include "ITransport.h"
#include "TransportPluginAbi.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace Graftcode::Plugins::TransportAbi
{
    using Hypertube::Native::Interfaces::ITransport;

    // Allocates a NUL-terminated copy released with TransportFreeBuffer.
    inline unsigned char* CopyBuffer(const void* data, std::size_t length)
    {
        auto* buffer = static_cast<unsigned char*>(std::malloc(length + 1));
        if (buffer == nullptr) {
            return nullptr;
        }
        if (length > 0) {
            std::memcpy(buffer, data, length);
        }
        buffer[length] = 0;
        return buffer;
    }

    inline char* CopyMessage(const std::string& message)
    {
        return reinterpret_cast<char*>(CopyBuffer(message.data(), message.size()));
    }

    template <typename Factory>
    TransportPluginChannel* CreateTransport(Factory&& factory, char** errorMessage)
    {
        if (errorMessage != nullptr) {
            *errorMessage = nullptr;
        }
        std::string error;
        try {
            ITransport* transport = factory();
            return reinterpret_cast<TransportPluginChannel*>(transport);
        }
        catch (const std::exception& e) {
            error = e.what();
        }
        catch (...) {
            error = "unknown transport plugin error";
        }
        if (errorMessage != nullptr) {
            *errorMessage = CopyMessage(error);
        }
        return nullptr;
    }

    inline void DestroyTransport(TransportPluginChannel* channel)
    {
        delete reinterpret_cast<ITransport*>(channel);
    }

    inline int32_t SendCommand(
        TransportPluginChannel* channel,
        const unsigned char* message,
        int32_t messageLen,
        unsigned char** response,
        int32_t* responseLen)
    {
        if (response == nullptr || responseLen == nullptr) {
            return TRANSPORT_PLUGIN_ERROR;
        }
        *response = nullptr;
        *responseLen = 0;

        std::string error;
        try {
            if (channel == nullptr) {
                throw std::invalid_argument("transport channel is null");
            }
            const std::vector<byte> result = reinterpret_cast<ITransport*>(channel)->SendCommand(
                const_cast<byte*>(message), messageLen);
            unsigned char* buffer = CopyBuffer(result.data(), result.size());
            if (buffer == nullptr) {
                throw std::bad_alloc();
            }
            *response = buffer;
            *responseLen = static_cast<int32_t>(result.size());
            return TRANSPORT_PLUGIN_OK;
        }
        catch (const std::exception& e) {
            error = e.what();
        }
        catch (...) {
            error = "unknown transport plugin error";
        }
        *response = CopyBuffer(error.data(), error.size());
        *responseLen = *response != nullptr ? static_cast<int32_t>(error.size()) : 0;
        return TRANSPORT_PLUGIN_ERROR;
    }

    inline void FreeBuffer(void* buffer)
    {
        std::free(buffer);
    }
}
