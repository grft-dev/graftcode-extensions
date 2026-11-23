# Payment plugins

Native payment provider plugins for Graftcode Gateway. The Gateway core implements the agent payment protocol (x402 over MCP) and loads one plugin per payment service through the C ABI in `GraftcodePluginsInterfaces/PaymentProviderAbi.h` (`LoadLibrary`/`dlopen`, JSON in and out). A plugin never sees the MCP protocol; it creates a checkout session for a quote and verifies it.

| Plugin | Plugin id | Service | Platforms | Docs |
|---|---|---|---|---|
| [zevio](zevio/Readme.md) | `payment.zevio` | Zevio checkout links (BLIK, card) | Windows (WinHTTP) | [Readme](zevio/Readme.md) |

Each plugin keeps its own copy of `GraftcodePluginsInterfaces/PaymentProviderAbi.h`, as the communication plugins do with their interface headers. The source of truth is `src/GraftcodeGateway/Payments/PaymentProviderAbi.h` in graftcode-gateway; the copies must stay identical to it.

Payment plugins are not part of the communication-plugins release workflow yet.
