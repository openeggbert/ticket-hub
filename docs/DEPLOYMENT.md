# Production deployment

Ticket Hub is a self-hosted application. The Compose file is production-safe
only after a unique database password is supplied; it deliberately exposes
neither PostgreSQL nor HTTP on a public interface.

## Start it

1. Copy `.env.example` to `.env` and replace
   `TICKETHUB_POSTGRES_PASSWORD` with a long, random secret. Leave
   `TICKETHUB_SEED_DEMO` at `false`: the demo seed creates a global
   administrator (`demo@ticket-hub.local`) whose password is published in
   `migrations/*/002_seed_demo.sql`. Ticket Hub refuses to start with seeding
   enabled on a non-loopback bind address, but if you have ever run an
   installation with it on, check the `users` table for the three
   `@ticket-hub.local` accounts and remove them -- turning the flag off does
   not delete rows that already exist.
2. Start the application: `docker compose up -d --build`.
3. Place a TLS-terminating reverse proxy in front of `127.0.0.1:8080`.
4. Create the first administrator with `docker compose exec ticket-hub
   ticket-hub-cli create-user "admin@example.com" "Admin" "a-long-password" --admin`.

For local development only, use `docker compose -f docker-compose.yml -f
docker-compose.dev.yml up --build`. The override deliberately makes
PostgreSQL available at `127.0.0.1:5432` with the non-production
`tickethub-dev` password.

## Reverse proxy requirements

The reverse proxy is not optional. Three of the requirements below are the
*only* mitigation for issues the application cannot fix in-process.

**TLS.** Terminate TLS at the proxy and forward to the loopback-only Ticket
Hub port. Both session cookies are `Secure` and carry the `__Host-` prefix,
so a browser will silently refuse to store them over plain HTTP on anything
other than localhost: served without TLS, the login request returns `200 OK`
and the user is immediately bounced back to the login screen with no
explanation. TLS is a functional requirement, not just a hardening step.

**Request body limit — required.** Set `client_max_body_size 25m;` (nginx) or
the equivalent. Crow buffers an entire request body before any handler runs,
so Ticket Hub's own 1 MiB JSON limit and 25 MB attachment limit (D98) are
both enforced *after* the memory has already been allocated -- as are the
CSRF check and the rate limiter. Without an edge limit, an unauthenticated
caller can drive the process to an out-of-memory kill: this was measured at
roughly one byte of resident memory per byte sent, with ten concurrent 100 MB
requests to `/api/v1/auth/login` taking the server past 1 GB. There is no
in-process fix; Crow's `SimpleApp` exposes no pre-buffer hook.

**HSTS and the server banner.** Add
`Strict-Transport-Security: max-age=31536000; includeSubDomains` at the
proxy -- the application does not emit it, since it does not know whether it
is being served over TLS. Also strip or rewrite the `Server: Crow/master`
response header (`proxy_hide_header Server;` plus `add_header Server ...;` in
nginx) so the HTTP stack and its version are not advertised.

**Rate limiting.** Do not rely on `X-Forwarded-For` to make Ticket Hub's
in-process rate limiter client-aware; the application intentionally does not
trust that header, so a client cannot spoof the key. The login limiter is
keyed on the target account plus the observed IP and only counts *failed*
attempts, so one account being attacked no longer starves every other user
arriving through the same proxy address. A loose per-IP ceiling (300 failed
attempts / 15 minutes) still sits behind it and will be shared by everyone
behind the proxy. Apply per-real-client rate limiting at the proxy if you
need it.

**Attachment storage.** `TICKETHUB_ATTACHMENTS_MAX_TOTAL_BYTES` caps total
stored attachment bytes (default 10 GiB, `0` disables the check). Without a
cap, any project Member can fill the volume -- the per-file and per-ticket
limits bound a single upload, but nothing bounds the number of tickets. Size
it below the actual volume capacity, since the database usually shares it.
Recycle-bin attachments count toward the cap: their files stay on disk until
a permanent delete.

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
