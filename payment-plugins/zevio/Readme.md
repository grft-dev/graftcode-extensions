# Zevio Payment Plugin (`payment.zevio`)

This plugin lets Graftcode Gateway take payments through [Zevio](https://zevio.io) for paid MCP tools. The Gateway speaks the agent payment protocol itself (x402 over MCP: `PaymentRequired`, signed challenge, proof in `params._meta["x402/payment"]`, receipt in `result._meta["x402/payment-response"]`). The plugin only knows Zevio. It creates a checkout link for a quote and tells the Gateway whether that link was paid.

It exports the payment provider C ABI from `GraftcodePluginsInterfaces/PaymentProviderAbi.h` (ABI version 1, JSON in and out):

| Export | What it does |
|---|---|
| `PaymentProviderAbiVersion`, `PaymentProviderId` | `1` and `"payment.zevio"`. The Gateway refuses a library that returns anything else. |
| `PaymentProviderCapabilities`, `PaymentProviderConfigSchema`, `PaymentProviderValidateConfig` | Scheme `zevio-checkout`, interaction `checkout_link`; JSON Schema of the config below; config check before an instance is created. |
| `CreatePaymentProvider`, `DestroyPaymentProvider` | One instance per configured provider. Instances are thread-safe. |
| `PaymentProviderCreateSession` | Reads the offer (`GET /qr/{qrId}`), requires it to be active and its gross price to equal the merchant quote (otherwise `price_mismatch`), then `POST /payments/{qrId}` with `metadata.challenge_id`. Returns `paymentUrl` (`destinationLink`) and `shortUrl` (`link`). |
| `PaymentProviderVerify` | `GET /payments?qrId=..&from=..&sortBy=createdAt&sortOrder=desc`, picks the payment whose `metadata.challenge_id` matches. `success` is `paid` (amount and currency checked); `pending`/`failed` are `pending` (the payer can retry); other statuses are `failed`. |
| `PaymentProviderFreeBuffer` | Frees every string the plugin returned. |

It is written in C++20. HTTPS goes through WinHTTP (system TLS), so the plugin builds on **Windows only**; Linux and macOS need a libcurl-based `WinHttpClient` replacement. CMake FetchContent builds nlohmann/json `v3.12.0` (header-only). The MSVC CRT is static (`/MT`), so the release archive is `payment_zevio.dll` only. The first configure needs network access.

`GraftcodePluginsInterfaces/PaymentProviderAbi.h` is a copy of `src/GraftcodeGateway/Payments/PaymentProviderAbi.h` from [graftcode-gateway](https://github.com/grft-dev/graftcode-gateway) (branch `task/payment-native-plugin`). Keep the two files identical; bump `GG_PAYMENT_PROVIDER_ABI_VERSION` in both when the ABI changes.

## Build

```powershell
cd graftcode-extensions\payment-plugins\zevio
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # from a "x64 Native Tools" prompt (vcvars64)
cmake --build build --config Release
```

Output: `payment-plugins/zevio/build/ZevioPlugin/payment_zevio.dll`. `cmake --install build --prefix <dir> --component plugin-runtime` copies only the DLL, like the communication plugins.

Download `gg` with payment support from the `task/payment-native-plugin` branch of graftcode-gateway (option `--paymentConfig`).

## Gateway

The plugin is loaded from the Gateway payment config (`gg ... --paymentConfig gg-payments.json`), not from `--config`. `library` is resolved relative to the config file; without it the Gateway looks for `payment_zevio.dll` next to the config file.

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
      "pricing": { "mode": "sdk", "class": "BarberShop.BarberQuoting", "method": "quote" },
      "interaction": "checkout_link"
    }
  }
}
```

The merchant quote returns a `reference` (here `haircut`); `offers` maps it to the Zevio offer `qrId`. The amount comes from the merchant; the plugin only checks that it equals the offer price, because `POST /payments/{qrId}` has no amount field.

## Configuration reference

| Field | Required | Default | Meaning |
|---|---|---|---|
| `apiKey` | yes | | Zevio API key (Bearer). Pass it as `{"secretRef":"env:NAME"}`; the Gateway resolves it. The plugin never logs it. |
| `offers` | yes | | Object: quote `reference` -> Zevio offer `qrId` (`qr_...`). |
| `environment` | no | `sandbox` | `sandbox` uses `https://api.sandbox.zevio.io/api/v1`. `production` requires an explicit `apiUrl`. |
| `apiUrl` | no | | API base URL override. |
| `customerEmail` | no | `poc-agent@example.com` | Payer e-mail sent with the payment. |
| `language` | no | `PL` | `PL` or `EN` checkout language. |
| `timeoutMs` | no | `30000` | HTTP timeout per request. |

Limits: no webhooks (status by polling `GET /payments`), no refunds, one offer = fixed price.
