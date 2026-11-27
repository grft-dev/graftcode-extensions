# Zevio Payment Plugin (`payment.zevio`)

This plugin lets Graftcode Gateway take payments through [Zevio](https://zevio.io) for paid MCP tools. The Gateway speaks the agent payment protocol itself (x402 over MCP: `PaymentRequired`, signed challenge, proof in `params._meta["x402/payment"]`, receipt in `result._meta["x402/payment-response"]`). The plugin only knows Zevio. It reads the price of a Zevio offer, creates a checkout link for a quote and tells the Gateway whether that link was paid.

It exports the payment provider C ABI from `GraftcodePluginsInterfaces/PaymentProviderAbi.h` (ABI version 2, JSON in and out):

| Export | What it does |
|---|---|
| `PaymentProviderAbiVersion`, `PaymentProviderId` | `2` and `"payment.zevio"`. The Gateway refuses a library that returns anything else. |
| `PaymentProviderCapabilities`, `PaymentProviderConfigSchema`, `PaymentProviderValidateConfig` | Scheme `zevio-checkout`, interaction `checkout_link`; JSON Schema of the config below; config check before an instance is created. |
| `CreatePaymentProvider`, `DestroyPaymentProvider` | One instance per configured provider. Instances are thread-safe. |
| `PaymentProviderQuote` | Pricing mode `offer` (the default). Resolves the offer key through `offers` (or takes a `qr_...` id as is), reads the offer (`GET /qr/{qrId}`), requires it to be active and returns its gross price (`totalAmount`), currency and `title`. |
| `PaymentProviderCreateSession` | In offer mode (`pricingMode` is `offer`), does not read the offer again: it goes straight to `POST /payments/{qrId}` with `metadata.challenge_id`. In sdk mode, it always reads a fresh `GET /qr/{qrId}`, requires the offer to be active and its gross price to equal the merchant quote (otherwise `price_mismatch`), then posts. Returns `paymentUrl` (`destinationLink`) and `shortUrl` (`link`). |
| `PaymentProviderVerify` | `GET /payments?qrId=..&from=..&sortBy=createdAt&sortOrder=desc`, picks the payment whose `metadata.challenge_id` matches. `success` is `paid` (amount and currency checked); `pending`/`failed` are `pending` (the payer can retry); other statuses are `failed`. |
| `PaymentProviderFreeBuffer` | Frees every string the plugin returned. |

It is written in C++20 and builds on Windows, Linux, and macOS. HTTPS goes through libcurl from vcpkg (`vcpkg.json`), linked into the plugin. Each call uses its own curl handle. On Windows the client resolves the system proxy with WinHTTP (automatic proxy, including a PAC script) and applies that proxy to the transfer. An intranet proxy authenticates with the current user's Negotiate or NTLM credentials, matching WinHTTP's default autologon policy. An internet proxy does not receive those credentials. CMake FetchContent builds nlohmann/json `v3.12.0` (header-only). On Windows the MSVC CRT is static (`/MT`) and vcpkg uses the `*-windows-static` triplet, so the release archive is the plugin library only: `payment_zevio.dll`, `payment_zevio.so`, or `payment_zevio.dylib`. The first configure needs network access.

`GraftcodePluginsInterfaces/PaymentProviderAbi.h` is a copy of `src/GraftcodeGateway/Payments/PaymentProviderAbi.h` from [graftcode-gateway](https://github.com/grft-dev/graftcode-gateway) (branch `task/payment-native-plugin`). Keep the two files identical; bump `GG_PAYMENT_PROVIDER_ABI_VERSION` in both when the ABI changes.

## Build

curl is declared in `vcpkg.json`. Point CMake at a vcpkg toolchain (GitHub-hosted runners set `VCPKG_INSTALLATION_ROOT`):

```bash
cd graftcode-extensions/payment-plugins/zevio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

Output: `payment-plugins/zevio/build/ZevioPlugin/payment_zevio.dll` (`.so` on Linux, `.dylib` on macOS). `cmake --install build --prefix <dir> --component plugin-runtime` copies only that library, like the communication plugins.

Download `gg` with payment support from the `task/payment-native-plugin` branch of graftcode-gateway (option `--paymentConfig`).

## Gateway

The plugin is loaded from the Gateway payment config (`gg ... --paymentConfig gg-payments.json`), not from `--config`. `library` is resolved relative to the config file; without it the Gateway looks for `payment_zevio.dll`, `payment_zevio.so`, or `payment_zevio.dylib` next to the config file.

```json
{
  "merchantId": "barber-001",
  "network": "zevio-sandbox",
  "challengeKey": { "secretRef": "env:GRAFTCODE_PAYMENT_HMAC_KEY" },
  "contextKey": { "secretRef": "env:GRAFTCODE_PAYMENT_CONTEXT_KEY" },
  "providers": {
    "zevio_barber": {
      "plugin": "payment.zevio",
      "library": "plugins/payment_zevio.dll",
      "config": {
        "environment": "sandbox",
        "apiKey": { "secretRef": "env:ZEVIO_API_KEY" },
        "customerEmail": "payer@example.com",
        "language": "PL",
        "offers": { "haircut": "qr_64cE47gBwhbH" }
      }
    }
  },
  "tools": {
    "reserve_barber": {
      "provider": "zevio_barber",
      "pricing": { "mode": "offer", "offer": "haircut" },
      "interaction": "checkout_link"
    }
  }
}
```

### Pricing

- **`offer` (default).** The price lives only in Zevio. The tool names an offer: a key of `offers` (here `haircut`) or a Zevio offer id (`qr_...`). On a 402 (no proof) the Gateway always calls `PaymentProviderQuote` (`GET /qr/{qrId}`) for the current price — a change in Zevio is visible on the next unpaid call, without restarting the Gateway. When the client sends a signed challenge, the Gateway uses the amount and currency from that challenge and does not quote again (`args_mismatch` and similar rejections do not call Zevio). If `pricing` is omitted, the mode is `offer` and the offer key is the tool name. Optional fields: `description` (default: the offer `title`) and `validForSeconds` (default `1800`).
- **`sdk` (dynamic prices).** `"pricing": { "mode": "sdk", "class": "BarberShop.BarberQuoting", "method": "quote" }`. The merchant module computes the quote from the tool arguments and returns a `reference`; `offers` maps it to the Zevio offer `qrId`. The plugin checks that the quoted amount equals the offer price (otherwise `price_mismatch`), because `POST /payments/{qrId}` has no amount field.

## Configuration reference

| Field | Required | Default | Meaning |
|---|---|---|---|
| `apiKey` | yes | | Zevio API key (Bearer). Pass it as `{"secretRef":"env:NAME"}`; the Gateway resolves it. The plugin never logs it. |
| `offers` | no | | Object: offer key (tool `pricing.offer` or `sdk` quote `reference`) -> Zevio offer `qrId` (`qr_...`). Without it, tools must use `qr_...` ids directly. |
| `environment` | no | `sandbox` | `sandbox` uses `https://api.sandbox.zevio.io/api/v1`. `production` requires an explicit `apiUrl`. |
| `apiUrl` | no | | API base URL override. |
| `customerEmail` | no | `poc-agent@example.com` | Payer e-mail sent with the payment. |
| `language` | no | `PL` | `PL` or `EN` checkout language. |
| `timeoutMs` | no | `30000` | HTTP timeout per request. `0` waits without a limit. |

Limits: no webhooks (status by polling `GET /payments`), no refunds, one offer = fixed price.
