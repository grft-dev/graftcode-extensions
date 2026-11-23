/**
 * @file PaymentProviderAbi.h
 * @brief C ABI exported by payment provider plugin libraries (e.g. payment_zevio.dll).
 *
 * "Protocol != provider": Graftcode Gateway core speaks the agent payment protocols (x402 over MCP,
 * later MPP / URL elicitation). A provider plugin only knows its payment service: it creates a payment
 * session (checkout link) for a quote and tells whether that session was paid.
 *
 * A plugin exports (extern "C", no C++ types cross the boundary):
 *  - PaymentProviderAbiVersion      -> must return GG_PAYMENT_PROVIDER_ABI_VERSION
 *  - PaymentProviderId              -> static NUL-terminated id, e.g. "payment.zevio" (not freed)
 *  - PaymentProviderCapabilities    -> JSON (schemes, interactions, currencies, verification mode)
 *  - PaymentProviderConfigSchema    -> JSON Schema of the provider config
 *  - PaymentProviderValidateConfig  -> {"ok":true} or {"ok":false,"errors":["..."]}
 *  - CreatePaymentProvider          -> opaque instance for a validated config (null + *errorJson on failure)
 *  - DestroyPaymentProvider
 *  - PaymentProviderCreateSession   -> JSON (see below)
 *  - PaymentProviderVerify          -> JSON (see below)
 *  - PaymentProviderFreeBuffer      -> releases every char* returned by the plugin
 *
 * All strings are UTF-8 JSON. Every char* returned by the plugin is allocated by the plugin and must be
 * released with PaymentProviderFreeBuffer. Instances must be thread-safe (GG calls them from many threads).
 * Secrets (e.g. API keys) arrive already resolved inside configJson; plugins must never log them.
 *
 * createSession request:
 *   {"sessionRef":"<challengeId>","quoteId":"q_..","merchantId":"..","tool":"..","amount":"50.00",
 *    "currency":"PLN","description":"..","reference":"haircut","expiresAt":"2026-11-23T13:00:00Z",
 *    "metadata":{"k":"v"}}
 * createSession response:
 *   {"ok":true,"scheme":"zevio-checkout","paymentUrl":"https://..","shortUrl":"https://..","sessionId":".."}
 *   {"ok":false,"error":{"code":"price_mismatch","message":".."}}
 *
 * verify request:
 *   {"sessionRef":"<challengeId>","amount":"50.00","currency":"PLN","reference":"haircut","issuedAt":"2026-11-23T12:30:00Z"}
 * verify response:
 *   {"status":"paid","paymentId":"pay_..","amount":"50.00","currency":"PLN","settlementStatus":"Settled"}
 *   {"status":"pending","detail":".."}
 *   {"status":"failed","error":"amount_too_low","detail":".."}
 */

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GG_PAYMENT_PROVIDER_ABI_VERSION 1u

/** Opaque provider instance owned by the plugin. */
typedef struct PaymentProviderInstance PaymentProviderInstance;

typedef uint32_t (*PaymentProviderAbiVersionFn)(void);
typedef const char* (*PaymentProviderIdFn)(void);
typedef char* (*PaymentProviderCapabilitiesFn)(void);
typedef char* (*PaymentProviderConfigSchemaFn)(void);
typedef char* (*PaymentProviderValidateConfigFn)(const char* configJson);
typedef PaymentProviderInstance* (*CreatePaymentProviderFn)(const char* configJson, char** errorJson);
typedef void (*DestroyPaymentProviderFn)(PaymentProviderInstance* instance);
typedef char* (*PaymentProviderCreateSessionFn)(PaymentProviderInstance* instance, const char* requestJson);
typedef char* (*PaymentProviderVerifyFn)(PaymentProviderInstance* instance, const char* requestJson);
typedef void (*PaymentProviderFreeBufferFn)(void* buffer);

#ifdef __cplusplus
}
#endif
