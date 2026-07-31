# Scope status

The current build target is the **reduced-scope V1**:
[../REDUCED_SCOPE_SPECIFICATION.md](../REDUCED_SCOPE_SPECIFICATION.md), roadmap in
[REDUCED_SCOPE_ROADMAP.md](REDUCED_SCOPE_ROADMAP.md), and table catalog in
[REDUCED_SCOPE_DATA_MODEL.md](REDUCED_SCOPE_DATA_MODEL.md). The original full-scope
[../SPECIFICATION.md](../SPECIFICATION.md), [ROADMAP.md](ROADMAP.md), and [DATA_MODEL.md](DATA_MODEL.md)
remain as the long-term aspirational baseline only — do not build against them directly.

## Implemented prototype slice

- C++20/CMake project and `TicketHub` namespace.
- Crow route layer and hybrid vanilla HTML/CSS/JS demo UI (route/UI source updated for Phase 1, but not
  compiled in any sandbox yet — see `docs/VERIFICATION.md`).
- PostgreSQL and SQLite database adapters behind one application-facing interface.
- Ordered checksummed schema migrations and idempotent demo seed.
- Projects and transactional project-local issue numbering.
- Fixed issue types/statuses/priorities used by the prototype.
- Issue creation/list/detail, status changes, labels and comments -- now principal-driven, not the fixed
  demo user (Phase 1).
- Issue version exposed for optimistic status-change conflict detection.
- Permanent issue-key alias lookup foundation.
- Recycle-bin columns and live-query filtering foundation.
- **Local-account identity (Phase 1):** UUID/email users (no `handle` yet), Argon2id password hashing,
  server-side sessions (SHA-256 token hash, 30-day fixed lifetime), minimal login-attempt lockout,
  administrator-only account creation via `ticket-hub-cli create-user`. No self-registration, no
  invitations, no OIDC, no forced-password-change flow -- see `REDUCED_SCOPE_SPECIFICATION.md` section 3.
- **Fixed project-role authorization and project lifecycle (Phase 2):** Viewer/Member/Admin roles plus a
  global-administrator bypass, enforced on every issue and project write; project create (global admin),
  archive/unarchive (project admin), soft delete/restore/permanent delete (project admin to bin, global
  admin to restore or purge), fixed 90-day on-demand recycle-bin retention; installation-wide anonymous
  read-access toggle, off by default (D59) -- every read use case now takes an optional `Principal`.
- **Fixed hierarchy and workflow rules (Phase 3, complete at the core/CLI/test layer):** the Epic ->
  Story/Task/Bug -> Sub-task hierarchy is enforced on issue creation (D5, D29, D64-D66); status
  transitions enforce the fixed workflow rules (D68-D70) -- resolution required on completion, cleared on
  reopen, and a parent cannot complete while any sub-task is unfinished; full-replacement issue edit with
  the same optimistic-locking contract as status changes (D129) -- summary, description, priority,
  assignee, story points, due date, labels, one `issue_history` row per changed field; the fixed
  issue-link catalog (D17) -- `blocks`/`relates_to`/`duplicates`/`clones`, visible from both ends,
  requiring project-Member-or-above on both linked issues' projects; simple field-copy cloning (D60) with
  an automatic `clones` link; self-service watching (D20) and voting (D79), the one issue write with no
  project-role check; the issue recycle bin (D22, mirrors the project one) -- soft delete by project
  admin, restore/list/permanent-delete by global admin only; simple bulk actions (D36) --
  status/assignee/label/recycle applied to a list of issue keys, each through the same single-issue
  operation, reporting partial success rather than rolling back; simple integer manual ordering with
  full renumbering on every move (D31), replacing the never-used `issues.rank_value` LexoRank placeholder;
  moving an issue to a different project (D37) -- no compatibility check needed since every project
  shares the same fixed types/workflow/fields, so a move is a `project_id` change plus a freshly
  allocated key/number, rejected if the issue has a parent or any children, requiring
  project-Member-or-above on both the source and target projects, with the vacated key becoming a
  permanent alias (D38). See "Not yet built" below for the rest of Phase 3.
- Domain, migration, crypto, identity, SQLite integration, authorization, and workflow tests (7/7
  passing; see `docs/VERIFICATION.md`).

## Not yet built (still V1 scope — see `REDUCED_SCOPE_ROADMAP.md`)

- The session-cookie/CSRF wiring and the Phase 2/3 route additions in `src/web/Api.cpp` have not been
  compiled or smoke-tested (Crow is unavailable in this sandbox); there is also no login page,
  project-management UI, or hierarchy/resolution/edit/link/clone/watch/vote/recycle-bin/bulk-action/
  reorder/move UI in `web/` yet.
- Active-session list / "sign out everywhere" and the full configurable lockout policy are resequenced
  to Phase 6, alongside REST rate limiting.
- Rest of Phase 3: re-typing or re-parenting an issue after creation (`editIssue` does not touch
  `issueTypeKey`/`parentIssueKey`).
- Comments/mentions/reactions, attachments, and the Kanban board are not implemented (Phase 4-5).
- The `/api/v1` REST surface, CSV export, backup/restore, and upgrade command are not implemented
  (Phase 6-7).
- Docker packaging and the hardening/accessibility passes are not done (Phase 8).

## Permanently out of V1 scope (do not build these)

OIDC, invitations, public registration, configurable permission/notification schemes, the configurable
workflow engine, custom fields, saved/shared filters, Scrum/sprints/agile reports, versions/releases,
issue templates, automation rules, webhooks, service accounts, Git integration, the Jira migration
tool, CSV import, outbound/inbound email, the background job queue, internal event bus, realtime
(SSE), in-memory cache, S3 attachment storage, Kubernetes/Helm/`.deb`/`.rpm` packaging, the i18n
framework, and pluggable secrets/observability backends. Full list with decision numbers:
[REMOVED_AND_DEFERRED_FEATURES.md](REMOVED_AND_DEFERRED_FEATURES.md).

Do not treat the current UI or `IDatabase` shape as the final API. They are a working vertical slice
used to evolve the architecture incrementally.
