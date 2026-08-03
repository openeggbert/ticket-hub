-- Durable outbox/delivery infrastructure (CLAUDE.md: "Durable side effects
-- must eventually use jobs/outbox/events. Do not use detached in-memory
-- tasks for email, webhooks..."), built to support two deferred-after-V1
-- features requested together: outbound webhooks (D39/D41) and outbound
-- email (D52). This app has no background worker process, so "the job
-- queue" is a plain table plus a new `ticket-hub-cli process-outbox`
-- command an admin crons -- the same "explicit admin-run CLI step" pattern
-- `migrate`/`backup`/`seed-demo` already established, not a new kind of
-- infrastructure. The server itself never makes an outbound network call:
-- request handlers only ever write a durable delivery row (fast, local,
-- transactional); all real webhook POSTs and SMTP sends happen later, only
-- from the CLI, so a slow or unreachable external endpoint can never block
-- a request-handling thread.
--
-- Two separate concrete tables, not one generic "outbox_events" table --
-- matching this codebase's existing preference for purpose-specific tables
-- (comments/worklogs/notifications are separate tables, not one generic
-- "activity" table) over a generic all-purpose abstraction.

-- Outbound webhooks (D39: "public REST API, PATs, service accounts, and
-- webhooks", grouped with the other installation-level integration
-- concepts, so subscriptions are global-admin-managed, not per-project).
-- `event_types` is a comma-joined list from a fixed set (`ticket.created`,
-- `ticket.status_changed`, `ticket.updated`, `comment.added`) -- empty
-- means every type. `project_key` is a nullable single-project filter (a
-- deliberate reduction from D41's full "structured visual filter" across
-- type/status/priority/assignee/standard+custom fields -- out of scope for
-- one batch). `secret` is the HMAC-SHA256 signing key, shown once at
-- creation like a personal access token (`015_personal_access_tokens.sql`).
CREATE TABLE IF NOT EXISTS webhook_subscriptions (
    id VARCHAR(36) PRIMARY KEY,
    target_url TEXT NOT NULL,
    secret TEXT NOT NULL,
    event_types TEXT NOT NULL DEFAULT '',
    project_key VARCHAR(12),
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_by_user_id VARCHAR(36) REFERENCES users(id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- The webhook outbox: one row per (subscription, event) pair queued for
-- delivery. `payload` is the exact JSON body that will be POSTed (built and
-- frozen at enqueue time, not recomputed at delivery time, so a later
-- ticket change can never alter what an already-queued delivery reports).
-- `status`: `pending` (not yet delivered, will be retried) ->
-- `delivered` (2xx response received) or `failed` (permanently gave up
-- after MaxDeliveryAttempts, see Domain::Models.h) -- `failed` is terminal,
-- there is no separate manual-retry API in this batch.
CREATE TABLE IF NOT EXISTS webhook_deliveries (
    id VARCHAR(36) PRIMARY KEY,
    subscription_id VARCHAR(36) NOT NULL REFERENCES webhook_subscriptions(id) ON DELETE CASCADE,
    event_type VARCHAR(40) NOT NULL,
    payload TEXT NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'delivered', 'failed')),
    attempt_count INTEGER NOT NULL DEFAULT 0,
    next_attempt_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_error TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    delivered_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_webhook_deliveries_pending ON webhook_deliveries(status, next_attempt_at);

-- Outbound email (D52: "pluggable delivery backend... prioritize
-- SMTP/sendmail first" -- SMTP only in this batch, no sendmail/provider-API
-- backend). No personal notification preferences (D86, confirmed moot) --
-- an email row is enqueued at exactly the same three trigger points that
-- already create an in-app notification (assigned/mentioned/watched
-- comment, D14), only when the installation has SMTP configured
-- (TICKETHUB_SMTP_HOST set), never per-user opt-in/out.
CREATE TABLE IF NOT EXISTS email_deliveries (
    id VARCHAR(36) PRIMARY KEY,
    recipient_user_id VARCHAR(36) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    subject TEXT NOT NULL,
    body TEXT NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'delivered', 'failed')),
    attempt_count INTEGER NOT NULL DEFAULT 0,
    next_attempt_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_error TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    sent_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_email_deliveries_pending ON email_deliveries(status, next_attempt_at);
