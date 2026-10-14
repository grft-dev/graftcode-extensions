/**
 * @file TransportPluginAbi.h
 * @brief C ABI exported by transport plugin libraries.
 *
 * A plugin exports:
 *  - CreateTransport
 *  - DestroyTransport
 *  - TransportSendCommand
 *  - TransportFreeBuffer
 *  - SupportsConcurrentRequests (optional; when missing or false, requests on one channel are serialized)
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

#define GRAFTCODE_TRANSPORT_OK 0
#define GRAFTCODE_TRANSPORT_ERROR (-1)

/** Opaque transport channel owned by the plugin. */
typedef struct GraftcodeTransportChannel GraftcodeTransportChannel;

/**
 * Creates a transport channel.
 * Returns null on failure; *errorMessage then receives a NUL-terminated message (or null).
 */
typedef GraftcodeTransportChannel* (*GraftcodeCreateTransportFn)(const char* ipAddress, unsigned short port, const char* configSource, char** errorMessage);

/** Destroys a channel returned by CreateTransport. */
typedef void (*GraftcodeDestroyTransportFn)(GraftcodeTransportChannel* channel);

/**
 * Sends a request and returns its response.
 * On GRAFTCODE_TRANSPORT_OK, *response and *responseLen hold the response (*response may be null when the length is 0).
 * Otherwise they hold a NUL-terminated error message and its length without the terminator.
 */
typedef int32_t (*GraftcodeTransportSendCommandFn)(GraftcodeTransportChannel* channel, const unsigned char* message, int32_t messageLen, unsigned char** response, int32_t* responseLen);

/** Releases a buffer returned by the plugin. */
typedef void (*GraftcodeTransportFreeBufferFn)(void* buffer);

/** Returns true when TransportSendCommand may be called concurrently on one channel. */
typedef bool (*GraftcodeSupportsConcurrentRequestsFn)(void);

#ifdef __cplusplus
}
#endif
