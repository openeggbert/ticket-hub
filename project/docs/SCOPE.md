# Scope status

The current build target is the **reduced-scope V1**:
[../REDUCED_SCOPE_SPECIFICATION.md](../REDUCED_SCOPE_SPECIFICATION.md), roadmap in
[REDUCED_SCOPE_ROADMAP.md](REDUCED_SCOPE_ROADMAP.md), and table catalog in
[REDUCED_SCOPE_DATA_MODEL.md](REDUCED_SCOPE_DATA_MODEL.md). The original full-scope
[../SPECIFICATION.md](../SPECIFICATION.md), [ROADMAP.md](ROADMAP.md), and [DATA_MODEL.md](DATA_MODEL.md)
remain as the long-term aspirational baseline only — do not build against them directly.

## Implemented prototype slice

- C++20/CMake project and `TicketHub` namespace.
- Crow route layer (all Phase 1-3 routes, built and live-verified against a real HTTP server — see
  "Server verification" in `README.md` and `docs/VERIFICATION.md`) and a hybrid vanilla HTML/CSS/JS demo
  UI that now covers every one of those routes: login, hierarchy/resolution pickers, full edit/clone/
  links/watch-vote/delete in the issue drawer, project management, the issue recycle bin, and reorder/
  move/bulk actions -- all browser-verified with Playwright/Chromium (`docs/VERIFICATION.md`).
- PostgreSQL and SQLite database adapters behind one application-facing interface.
- Ordered checksummed schema migrations and idempotent demo seed.
- Projects and transactional project-local issue numbering.
- Fixed issue types/statuses/priorities used by the prototype.
- Issue creation/list/detail, status changes, labels and comments -- now principal-driven, not the fixed
  demo user (Phase 1).
- Issue version exposed for optimistic status-change conflict detection.
- Permanent issue-key alias lookup, now actually written to by `moveIssue` (D37/D38, Phase 3).
- Recycle-bin columns and live-query filtering, now a real recycle bin for both projects and issues.
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
- **Comment editing and tombstone delete (Phase 4, partial, D81/D82/D83):** an `edited_at` timestamp
  instead of a version-history table; soft-delete via the same `deleted_at`/`deleted_by_user_id` columns
  issues and projects already use, with no separate admin recycle-bin API for comments (the row and
  original body simply remain in the database, excluded from ordinary listing); simplified permissions --
  the comment's own author can always edit/delete it, otherwise the actor needs project-Admin-or-above (or
  global admin), no separate edit-own/edit-all/delete-own/delete-all matrix. Same optimistic-locking
  contract (`expectedVersion` -> `Domain::ConcurrencyConflict`, 409) as issue edits.
- **Fixed emoji reactions on comments (Phase 4, partial, D84):** a `comment_reactions` many-to-many table
  (mirroring `issue_watchers`/`issue_votes`) with a fixed eight-key reaction set (GitHub's own set,
  chosen as a conservative default since the decision register does not enumerate one); self-service,
  no project-role check, same reasoning as watch/vote (D20/D79); idempotent add/remove.
- **@mention handles and the fixed in-app notification set (Phase 4, partial, D56/D80/D14):** an optional,
  unique `users.handle` (admin-set at account creation, no self-service profile editing yet); comments
  are scanned once, at creation, for `@handle` tokens, notifying each resolved user; three fixed
  notification types only -- assigned, mentioned, comment on a watched issue -- no email, no
  admin-configurable schemes, no per-user preferences/digests; a recipient who is both mentioned and
  watching the same comment gets one notification, not two. `GET /api/users` backs @mention autocomplete
  in the comment textarea; a notification bell with an unread badge in the UI opens a panel that marks
  notifications read on click and navigates to the related issue.
- **Rendered Markdown, visual toolbar, and live preview (Phase 4, D16):** comment bodies and issue
  descriptions render as formatted HTML (bold/italic/inline code/links/headings/lists/blockquotes/fenced
  code/rules) via a deliberately small, safe-by-construction subset -- input is HTML-escaped first, then
  wrapped in a fixed set of hardcoded tags, so there is no separate sanitization pass to get wrong.
  Bold/Italic/Code/Link/list/Quote toolbar buttons plus a live-preview toggle on every Markdown-capable
  textarea.
- **Simplified worklogs (Phase 4, D12/D13):** time spent + an optional comment only, no remaining-estimate
  linkage (D12 dropped time estimates from V1 entirely); no own-vs-others edit/delete permission split
  (D13) -- any project member with issue access may edit or delete any worklog on that issue, not just
  the one they logged themselves, unlike comments' author-or-admin rule (D83). Tombstone delete and the
  same optimistic-locking contract as comment/issue edits.
- **Simple append-only admin/security audit log (Phase 4, D23):** an `audit_events` table with no
  categories-as-a-retention-feature, export, or configurable retention -- rows are never purged.
  Recorded automatically for a small, focused set of admin/security-relevant writes (failed/blocked
  login, user creation, the anonymous-read toggle, permanently deleting a project or issue), not as a
  general-purpose hook on every write. Global-administrator-only read access, via a new "Audit log" nav
  item in `web/`. **This closes out Phase 4 (Collaboration) -- every item in
  `docs/REDUCED_SCOPE_ROADMAP.md`'s Phase 4 list is now implemented.**
- **Ad-hoc issue filter/search widening (Phase 5, D10/D43):** `GET /api/issues` and `Domain::IssueFilter`
  now also support type/priority/assignee/label/due-date filters (in addition to project/status/search),
  and search now also matches issue description, not just summary/key. Still ad-hoc, in-UI-only filters
  (no saved/shared filters, no JQL, not usable as a webhook/board source) and still a plain `LIKE`/`ILIKE`
  substring match (no full-text index). This is the first Phase 5 slice.

## Not yet built (still V1 scope — see `REDUCED_SCOPE_ROADMAP.md`)

- Active-session list / "sign out everywhere" and the full configurable lockout policy are resequenced
  to Phase 6, alongside REST rate limiting.
- Rest of Phase 3: re-typing or re-parenting an issue after creation (`editIssue` does not touch
  `issueTypeKey`/`parentIssueKey`).
- Phase 4 is complete. Phase 5 (Attachments and Kanban board) is started (filter/search widening done);
  dashboard personalization (D24), Kanban WIP limits/drag-and-drop (D33), and attachments
  (D15/D98-D105) are not implemented yet.
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
