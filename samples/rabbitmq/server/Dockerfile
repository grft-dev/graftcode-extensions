FROM debian:bookworm-slim AS downloader

ARG TARGETARCH
ARG PLUGIN_NAME

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    case "${TARGETARCH}" in \
      amd64) gateway_arch="amd64" ;; \
      arm64) gateway_arch="arm64" ;; \
      *) echo "Unsupported architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    mkdir -p /downloads/gateway /out; \
    curl -fsSL "https://github.com/grft-dev/graftcode-gateway/releases/latest/download/gg_linux_${gateway_arch}.tar.gz" \
      | tar -xz -C /downloads/gateway; \
    find /downloads/gateway -type f -name gg -exec cp {} /out/gg \;; \
    test -f /out/gg; \
    chmod +x /out/gg

RUN set -eux; \
    case "${TARGETARCH}" in \
      amd64) plugin_arch="x86_64" ;; \
      arm64) plugin_arch="arm64" ;; \
      *) echo "Unsupported architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    mkdir -p /downloads/plugin; \
    curl -fsSL "https://github.com/grft-dev/graftcode-extensions/releases/latest/download/${PLUGIN_NAME}-linux-${plugin_arch}.tar.gz" \
      | tar -xz -C /downloads/plugin; \
    cp -a /downloads/plugin/. /out/

FROM mcr.microsoft.com/dotnet/runtime:10.0

WORKDIR /app
COPY --from=downloader /out/ ./
COPY server/netapp.dll ./
COPY server/pluginconfig.json ./

EXPOSE 10301 10401

ENTRYPOINT ["./gg", "netapp.dll", "--port", "10301", "--httpport", "10401", "--config", "pluginconfig.json"]
