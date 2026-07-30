# Known constraints and risks

## Target versus implementation

The specification is intentionally much larger than the current code. Treat it as the approved destination, not as a claim of completeness.

## Database parity

PostgreSQL and SQLite must expose the same application-level behavior. They may use different SQL, locking, full-text, job, event, and backup implementations. SQLite is restricted to a single Ticket Hub process and limited worker concurrency.

## Migration immutability

Once a migration is released or applied, never edit it. Add a new ordered migration. The migration runner stores checksums and rejects modified history.

## Workflow complexity

The approved workflow system is substantial: drafts, publishing, version history, transition conditions, validators, ordered post-functions, and transition-local forms. Implement it as a dedicated domain module, not as scattered status-changing conditionals.

## Authorization must be centralized

Permission schemes affect every project/issue operation. Avoid route-local ad hoc checks. Build a single authorization service that consumes a principal, resource context, and permission key.

## History and reporting

Agile reports, comment history, audit retention, sprint scope history, and optimistic conflict views require structured immutable events/history. Do not rely on display strings as the sole source of truth.

## Side effects

Email, notifications, webhooks, search indexing, audit retention, imports, exports, and automations must eventually use durable jobs/outbox events. Do not use detached in-memory work for behavior that must survive restart.

## Security-sensitive areas

- Argon2id password handling.
- Session token storage and cookie attributes.
- PAT/service token hashing, scopes, expiry, and rotation.
- OIDC issuer/audience/nonce/state validation.
- Markdown sanitization and attachment access checks.
- Inbound email spoofing, deduplication, and atomic processing.
- Secrets must not appear in logs or backups in plaintext.

## Product choices that intentionally differ from full Jira

- Software projects only initially.
- Company-managed only initially.
- No JQL; visual/form-based filters only.
- No Jira screens/screen schemes; field contexts and simple create/edit/view flags.
- Board column maps to exactly one workflow status.
- One component at most per issue.
- No issue-level security schemes.
- One fixed personal dashboard.
- No SLA.
- No recurring issues.
- No threaded comments.
- No separate checklist entities.
- Fixed priorities, resolutions, and issue types initially.

## Potential specification tension already resolved

An early choice allowed hierarchy levels above Epic, but the later fixed issue-type decision limits the initial product to Epic, Story, Task, Bug, and Sub-task. Therefore: implement only Epic → standard issue → Sub-task now; keep the data model extensible for upper levels later.
