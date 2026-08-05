-- REST write idempotency keys (D128, deferred-after-V1, user-requested).
-- Scoped to the small set of POST routes that create a new, independently
-- visible resource (ticket, project, comment, worklog, ticket clone) -- the
-- classic double-submit/network-retry duplicate-record risk D128 explicitly
-- accepted for V1. A client sends the same `Idempotency-Key` header on a
-- retried request; the server replays the original response instead of
-- creating a second resource. Not wired into PATCH/DELETE/bulk/status-change
-- routes: those already converge to the same end state on repeat, so there
-- is no duplicate-record risk for a key to prevent there.
--
-- `request_hash` (SHA-256 of the raw request body) guards against a client
-- reusing the same key for a genuinely different request -- a real client
-- bug, not a legitimate retry -- which is rejected with 409 rather than
-- silently replaying the wrong cached response. Only successful (2xx)
-- responses are ever cached: if the original attempt failed validation, no
-- side effect happened, so a retry with the same key should simply run
-- normally rather than replay a cached error forever.
--
-- No expiry/purge job in this batch -- V1 has no background job
-- infrastructure (docs/REMOVED_AND_DEFERRED_FEATURES.md D51) -- rows
-- accumulate for the lifetime of the installation, the same posture as
-- audit_events.
CREATE TABLE IF NOT EXISTS idempotency_keys (
    user_id VARCHAR(36) NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    idempotency_key VARCHAR(200) NOT NULL,
    request_hash VARCHAR(64) NOT NULL,
    response_status INTEGER NOT NULL,
    response_body TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, idempotency_key)
);
