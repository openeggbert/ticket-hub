# Production deployment

Ticket Hub is a self-hosted application. The Compose file is production-safe
only after a unique database password is supplied; it deliberately exposes
neither PostgreSQL nor HTTP on a public interface.

## Start it

1. Copy `.env.example` to `.env` and replace
   `TICKETHUB_POSTGRES_PASSWORD` with a long, random secret.
2. Start the application: `docker compose up -d --build`.
3. Place a TLS-terminating reverse proxy in front of `127.0.0.1:8080`.
4. Create the first administrator with `docker compose exec ticket-hub
   ticket-hub-cli create-user "admin@example.com" "Admin" "a-long-password" --admin`.

For local development only, use `docker compose -f docker-compose.yml -f
docker-compose.dev.yml up --build`. The override deliberately makes
PostgreSQL available at `127.0.0.1:5432` with the non-production
`tickethub-dev` password.

## Reverse proxy requirements

Terminate TLS at the proxy and forward to the loopback-only Ticket Hub port.
Enforce a request body limit of **25 MB** at that proxy: Ticket Hub applies
its JSON and attachment limits after Crow has buffered the request, so the
edge limit is the protection against oversized socket payloads. Nginx's
relevant directive is `client_max_body_size 25m;`.

Do not rely on `X-Forwarded-For` to make Ticket Hub's in-process rate limiter
client-aware; the application intentionally does not trust that header. Apply
per-client rate limiting at the reverse proxy if required.

## Outbox worker

Webhook and email rows are durable, but delivery is intentionally separated
from HTTP request handling. Enable the bounded worker with:

```sh
docker compose --profile outbox up -d
```

It invokes `ticket-hub-cli process-outbox` every 60 seconds by default. Set
`TICKETHUB_OUTBOX_INTERVAL_SECONDS` to adjust the cadence. A systemd timer or
cron may invoke the same CLI command instead; do not run both schedulers at
once. The in-app **Outbox** administration page shows queued, delivered and
failed rows and supports a deliberate retry of terminal failures.
