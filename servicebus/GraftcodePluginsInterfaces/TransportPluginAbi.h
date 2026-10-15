/**
 * @file TransportPluginAbi.h
 * @brief C ABI exported by transport plugin libraries.
 *
 * A plugin exports:
 *  - CreateTransport
 *  - DestroyTransport
 *  - TransportSendCommand
 *  - TransportFreeBuffer
 *
 * Plugins must be thread-safe: TransportSendCommand may be called concurrently on one channel.
 *
 * Every buffer returned by the plugin is allocated by the plugin and released with TransportFreeBuffer.
 */

#pragma once
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSPORT_PLUGIN_OK 0
#define TRANSPORT_PLUGIN_ERROR (-1)

/** Opaque transport channel owned by the plugin. */
typedef struct TransportPluginChannel TransportPluginChannel;

/**
 * Creates a transport channel.
 * Returns null on failure; *errorMessage then receives a NUL-terminated message (or null).
 */
typedef TransportPluginChannel* (*TransportPluginCreateFn)(const char* ipAddress, unsigned short port, const char* configSource, char** errorMessage);

/** Destroys a channel returned by CreateTransport. */
typedef void (*TransportPluginDestroyFn)(TransportPluginChannel* channel);

/**
 * Sends a request and returns its response. May be called concurrently on one channel.
 * On TRANSPORT_PLUGIN_OK, *response and *responseLen hold the response (*response may be null when the length is 0).
 * Otherwise they hold a NUL-terminated error message and its length without the terminator.
 */
typedef int32_t (*TransportPluginSendCommandFn)(TransportPluginChannel* channel, const unsigned char* message, int32_t messageLen, unsigned char** response, int32_t* responseLen);

/** Releases a buffer returned by the plugin. */
typedef void (*TransportPluginFreeBufferFn)(void* buffer);

#ifdef __cplusplus
}
#endif
