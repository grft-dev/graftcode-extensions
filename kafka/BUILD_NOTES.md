# Kafka plugin build notes

- librdkafka, nlohmann/json, LZ4 (bundled), and OpenSSL are linked statically. Windows also links zlib statically.
- Windows OpenSSL and zlib come from vcpkg (`*-windows-static`, `/MT`). The Program Files OpenSSL is `/MD` and is not used. Pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.
- Linux and macOS link `libssl.a` and `libcrypto.a` into the plugin (`libssl-dev` or Homebrew `openssl`). The plugin does not import `libssl.so`.
- `libz` and `libzstd` stay shared on Linux and macOS. Those distro static archives are not built with `-fPIC`, so they cannot be linked into the plugin.
- Cyrus SASL stays a shared library on Linux (`libsasl2.so`). Its mechanisms are loaded as plugins at runtime. That library imports `libcrypto.so.3`, so the Linux archive still contains `libcrypto.so.3` for SASL even though the plugin's own TLS is static. Windows SASL is `secur32`. macOS builds that do not find Cyrus SASL omit it.
- librdkafka's libcurl OIDC helper is off, so the plugin does not depend on `libcurl`.
- Smoke test links factory exports; full RPC needs a live broker (`samples/kafka`).
- First configure needs network/git for FetchContent downloads.
