/**
 * @file PaymentProviderAbi.h
 * @brief C ABI exported by payment provider plugin libraries (e.g. payment_zevio.dll).
 *
 * "Protocol != provider": Graftcode Gateway core speaks the agent payment protocols (x402 over MCP,
 * later MPP / URL elicitation). A provider plugin only knows its payment service: it reads the price of an
 * offer defined in that service, creates a payment session (checkout link) for a quote and tells whether
 * that session was paid.
 *
 * A plugin exports (extern "C", no C++ types cross the boundary):
 *  - PaymentProviderAbiVersion      -> must return GG_PAYMENT_PROVIDER_ABI_VERSION
 *  - PaymentProviderId              -> static NUL-terminated id, e.g. "payment.zevio" (not freed)
 *  - PaymentProviderCapabilities    -> JSON (schemes, interactions, currencies, verification mode)
 *  - PaymentProviderConfigSchema    -> JSON Schema of the provider config
 *  - PaymentProviderValidateConfig  -> {"ok":true} or {"ok":false,"errors":["..."]}
 *  - CreatePaymentProvider          -> opaque instance for a validated config (null + *errorJson on failure)
 *  - DestroyPaymentProvider
 *  - PaymentProviderQuote           -> JSON (see below; ABI v2)
 *  - PaymentProviderCreateSession   -> JSON (see below)
 *  - PaymentProviderVerify          -> JSON (see below)
 *  - PaymentProviderFreeBuffer      -> releases every char* returned by the plugin
 *
 * All strings are UTF-8 JSON. Every char* returned by the plugin is allocated by the plugin and must be
 * released with PaymentProviderFreeBuffer. Instances must be thread-safe (GG calls them from many threads).
 * Secrets (e.g. API keys) arrive already resolved inside configJson; plugins must never log them.
 *
 * quote request (tool pricing mode "offer", the default: the price comes from the provider's offer):
 *   {"merchantId":"..","tool":"reserve_barber","offer":"haircut","arguments":{..}}
 *   "offer" is a key of the provider's offer map or a provider offer id (e.g. Zevio "qr_...").
 * quote response:
 *   {"ok":true,"amount":"50.00","currency":"PLN","description":"..","reference":"haircut","offerId":"qr_.."}
 *   {"ok":false,"error":{"code":"offer_inactive","message":".."}}
 *   GG uses amount/currency/description for the quote and passes "reference" back in createSession and verify.
 *
 * createSession request (amount/currency come from quote in "offer" mode or from the merchant module in "sdk" mode):
 *   {"sessionRef":"<challengeId>","quoteId":"q_..","merchantId":"..","tool":"..","amount":"50.00",
 *    "currency":"PLN","description":"..","reference":"haircut","pricingMode":"offer",
 *    "expiresAt":"2026-11-23T13:00:00Z","metadata":{"k":"v"}}
 *   "pricingMode" is "offer" or "sdk". If it is omitted or empty, the plugin treats it as "offer".
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

/** v2: adds PaymentProviderQuote (price read from a provider offer). */
#define GG_PAYMENT_PROVIDER_ABI_VERSION 2u

/** Opaque provider instance owned by the plugin. */
typedef struct PaymentProviderInstance PaymentProviderInstance;

typedef uint32_t (*PaymentProviderAbiVersionFn)(void);
typedef const char* (*PaymentProviderIdFn)(void);
typedef char* (*PaymentProviderCapabilitiesFn)(void);
typedef char* (*PaymentProviderConfigSchemaFn)(void);
typedef char* (*PaymentProviderValidateConfigFn)(const char* configJson);
typedef PaymentProviderInstance* (*CreatePaymentProviderFn)(const char* configJson, char** errorJson);
typedef void (*DestroyPaymentProviderFn)(PaymentProviderInstance* instance);
typedef char* (*PaymentProviderQuoteFn)(PaymentProviderInstance* instance, const char* requestJson);
typedef char* (*PaymentProviderCreateSessionFn)(PaymentProviderInstance* instance, const char* requestJson);
typedef char* (*PaymentProviderVerifyFn)(PaymentProviderInstance* instance, const char* requestJson);
typedef void (*PaymentProviderFreeBufferFn)(void* buffer);

#ifdef __cplusplus
}
#endif
