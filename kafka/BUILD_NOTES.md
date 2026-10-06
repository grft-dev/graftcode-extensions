# Kafka plugin build notes

- Dependencies: CMake `FetchContent` only (nlohmann/json + librdkafka). No vcpkg.
- Smoke test links factory exports; full RPC needs a live broker (`samples/kafka`).
- First configure needs network/git for FetchContent downloads.
