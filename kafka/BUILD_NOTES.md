# Kafka plugin build notes

Third-party code is linked into the plugin. The release archive is the plugin
library plus, on Linux only, Cyrus SASL:

- librdkafka and nlohmann/json are static (FetchContent). LZ4 and Snappy are the copies compiled into librdkafka.
- zlib (`v1.3.1`) and zstd (`v1.5.6`) are FetchContent static libraries built with position-independent code. Distro `libz.a` and `libzstd.a` are not PIC, so they are not used.
- OpenSSL is static. Unix builds use the distro or Homebrew `libssl.a` / `libcrypto.a` (those archives are PIC). Windows uses vcpkg `*-windows-static` from `kafka/vcpkg.json` because the OpenSSL under `Program Files` is `/MD`. Pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` on Windows.
- librdkafka's libcurl OIDC helper is off.

Shared libraries that stay in the Linux archive, and why:

- `libsasl2.so` loads SASL mechanism modules at runtime. A static `libsasl2.a` would still need those modules from the host, so the shared library stays.
- `libcrypto.so.3` is imported by that `libsasl2.so`, not by the plugin. The plugin's own TLS uses the static OpenSSL, and `--exclude-libs` keeps those symbols inside the plugin.

Windows SASL is `secur32` (system). macOS builds that do not find Cyrus SASL omit it. The MSVC CRT is `/MT`, so the VC++ Redistributable is not required.

- Smoke test links factory exports; full RPC needs a live broker (`samples/kafka`).
- First configure needs network/git for FetchContent downloads.
