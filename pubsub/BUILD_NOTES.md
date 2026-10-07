# Pub/Sub plugin build notes

Dependency choice: vcpkg `curl`, `openssl`, and `nlohmann-json` (same toolchain file as `sqs/` and `servicebus/`). The plugin speaks the Google Cloud Pub/Sub JSON REST API.

`google-cloud-cpp` (FetchContent or the vcpkg `pubsub` feature) pulls gRPC, protobuf, and Abseil. That superbuild is too heavy for this repo's six-platform CI matrix: multi-gigabyte RAM spikes and long compiles make the jobs unreliable. The REST client covers the emulator (`PUBSUB_EMULATOR_HOST`) and production (service-account JWT or a bearer token) without that stack.

- Windows builds use the static vcpkg triplet (`x64-windows-static` / `arm64-windows-static`) and `/MT`. curl, OpenSSL, and zlib are linked into `PubSubPlugin.dll`. curl still uses Schannel, so certificate verification stays on the Windows store. `OpenSSL::Crypto` is linked for RS256 JWTs. The DLL does not import `vcruntime140`, `libcurl`, `libcrypto`, or `libssl`.
- Hermetic `ctest` does not need an emulator. Invalid config, base64, JWT signing, and factory tests always run.
- `PubSubLive.RpcRoundTrip` runs only when `PUBSUB_EMULATOR_HOST` or `PUBSUB_LIVE` is set.
- First configure with the vcpkg toolchain needs network access so vcpkg can fetch ports. The manifest baseline matches `sqs/vcpkg.json`.
