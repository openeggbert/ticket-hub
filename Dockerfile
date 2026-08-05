# Official Docker image (D50): the only supported distribution path for
# V1 -- no .deb/.rpm, no Helm/Kubernetes. Two stages: build with the full
# toolchain (including a network-fetched Crow via CMake FetchContent), then
# copy only the installed binary/web/migrations into a slim runtime image
# with just the shared libraries the binary links against.

FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates \
        libpq-dev libsqlite3-dev libargon2-dev libasio-dev libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Server target only -- the CLI is also built by this same configuration
# (TICKETHUB_BUILD_CLI defaults ON) and installed alongside it, so both
# `ticket-hub` and `ticket-hub-cli` end up in the runtime image below.
# `ticket-hub-cli` (only) links libcurl for `process-outbox` (D39/D41
# webhooks, D52 email) -- run it via `docker run/exec ... ticket-hub-cli
# process-outbox` on an external schedule; the server itself makes no
# outbound network calls.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTICKETHUB_BUILD_SERVER=ON \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build --prefix /opt/ticket-hub

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libpq5 libsqlite3-0 libargon2-1 libcurl4 curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /home/ticket-hub --shell /usr/sbin/nologin ticket-hub

COPY --from=build /opt/ticket-hub /opt/ticket-hub

# Compiled-in defaults for TICKETHUB_WEB_ROOT/MIGRATIONS_ROOT point at the
# *build machine's* source checkout (see src/config/Config.cpp), which does
# not exist in this image -- must be overridden explicitly to the installed
# locations. TICKETHUB_ATTACHMENTS_DIR similarly defaults to a
# cwd-relative path; pointed here at a dedicated volume mount point (D15:
# local filesystem attachment storage only).
ENV TICKETHUB_WEB_ROOT=/opt/ticket-hub/share/ticket-hub/web \
    TICKETHUB_MIGRATIONS_ROOT=/opt/ticket-hub/share/ticket-hub/migrations \
    TICKETHUB_ATTACHMENTS_DIR=/data/attachments \
    TICKETHUB_BIND_ADDRESS=0.0.0.0 \
    TICKETHUB_PORT=8080 \
    PATH=/opt/ticket-hub/bin:$PATH

RUN mkdir -p /data/attachments && chown -R ticket-hub:ticket-hub /data

USER ticket-hub
WORKDIR /home/ticket-hub
VOLUME ["/data"]
EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s --start-period=10s \
    CMD curl -fsS "http://127.0.0.1:${TICKETHUB_PORT}/api/health" || exit 1

ENTRYPOINT ["ticket-hub"]
