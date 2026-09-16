# Build notes

## Verified in this workspace (2026-09-16 Europe/Warsaw)

| Step | Result |
|------|--------|
| `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` | **Succeeded** (FetchContent nlohmann/json + librdkafka `v2.8.0`) |
| `cmake --build build -j$(nproc)` | **Succeeded** → `build/KafkaPlugin/libKafkaPlugin.so` |
| `ctest --test-dir build` | **Passed** (`KafkaPluginSmoke`) |
| Exported symbols | `CreateServer`, `DestroyServer`, `CreateTransportChannel`, `DestroyTransportChannel` |

Toolchain used: GCC 14, CMake 3.31, OpenSSL 3.5, libsasl2, zlib, zstd, libcurl.

## Include path note

FetchContent builds expose `<rdkafkacpp.h>`; packaged/vcpkg installs typically use
`<librdkafka/rdkafkacpp.h>`. Sources use `__has_include` to accept either.

## Possible blockers elsewhere

1. **Network / git** required on first configure for FetchContent.
2. **librdkafka compile time** is several minutes; if the build is OOM-killed, use `cmake --build build -j2`.
3. Missing SSL/SASL packages: install `libssl-dev`, `libsasl2-dev`, `zlib1g-dev`, `libzstd-dev` (and optionally `libcurl4-openssl-dev`).
4. **Windows**: prefer vcpkg + `-DKAFKA_USE_SYSTEM_RDKAFKA=ON` with the vcpkg toolchain.
5. Smoke test must **not** call `IServer::start()` or `SendCommand` without a broker (reconnect / RPC timeout).

## Suggested PR checklist

- [x] Source complete (client + server RPC with correlation-id / reply-to)
- [x] CMake FetchContent + vcpkg.json
- [x] Linux configure/build/smoke in this environment
- [ ] Manual GG round-trip with `docker compose up -d` + `./scripts/create-topics.sh`
- [ ] Copy folder contents to `graftcode-extensions/kafka/` (flat drop-in; do not nest an extra sketch directory)
