# Ticket Hub V1 data model (reduced scope)

This supersedes `DATA_MODEL.md` as the target schema catalog for V1. `DATA_MODEL.md` remains the
long-term aspirational catalog (~90 tables) for post-V1 phases and is not deleted. This document walks
every table in that catalog and says whether it exists in V1, in what (possibly simplified) form, or
not at all — with the decision (`D#`) that made the call. `docs/SCHEMA.md` remains the record of what
is *physically implemented right now* in the 0.2.0 prototype; this document is the *target* for V1.

Conventions (unchanged from `DATA_MODEL.md`): UUID primary keys, `created_at`/`updated_at`, `version`
for optimistically-locked entities, `deleted_at`/`deleted_by_user_id` for soft-deletable records.

## A. System and operations

| Table | V1 status | Notes |
|---|---|---|
| `schema_migrations` | **Kept, unchanged** | Already implemented. |
| `installation_settings` | **Kept, minimal** | Small generic key/value table; used for the handful of remaining installation-level toggles (e.g. anonymous read-access enabled, D59). No secret-reference values (D134). |
| `secret_references` | **Removed** | Secrets come from environment variables only, D134. |
| `idempotency_keys` | **Removed** | No `Idempotency-Key` mechanism, D128. |
| `durable_events` | **Removed** | No internal event bus, D131. |
| `event_consumers` | **Removed** | No internal event bus, D131. |
| `background_jobs` | **Removed** | No job queue; everything is synchronous, D51. |
| `job_attempts` | **Removed** | Depends on `background_jobs`, D51. |
| `audit_events` | **Kept, simplified** | Same shape, but no category-based retention enforcement — rows are simply append-only and never auto-purged, D23. |
| `audit_retention_policies` | **Removed** | No configurable per-category retention, D23. |

## B. Identity and authentication

| Table | V1 status | Notes |
|---|---|---|
| `users` | **Kept, simplified** | UUID, unique `email`, optional unique `handle`, `display_name`, `time_zone`, `clock_format`, `active` (D56, D45). No generic `locale` field for translated UI text — English only, D44. |
| `local_credentials` | **Kept, unchanged** | Argon2id hash, D53. |
| `password_reset_tokens` | **Removed** | Password reset is administrator-performed (sets a temporary password directly), not a self-service token flow, D53. |
| `oidc_providers` | **Removed** | No OIDC in V1, D1. |
| `external_identities` | **Removed** | No OIDC in V1, D1. |
| `invitations` | **Removed** | Admin-created accounts only; no invitation flow, D2. |
| `sessions` | **Kept, unchanged** | Server-side cookie sessions, D54. |
| `groups` | **Removed** | Nothing left to grant permissions to a group for — fixed roles replaced schemes (D3) and there is no OIDC group mapping to populate them from (D1). |
| `group_members` | **Removed** | Depends on `groups`. |
| `service_accounts` | **Removed** | No service accounts; only user-owned PATs, D39. |
| `api_tokens` | **Kept, simplified** | Hashed token value, `expires_at`, `revoked_at`, `last_used_at`. No `scopes` column (a token always carries its owner's permissions) and no rotation-state columns, D40. |
| `account_merge_events` | **Removed** | No account merge, D57. |
| `user_preferences` | **Removed as a separate table** | The only preferences that survived (`time_zone`, `clock_format`) live directly on `users`; there is no per-event notification preference to store, D86. |

## C. Projects and authorization

| Table | V1 status | Notes |
|---|---|---|
| `projects` | **Kept, simplified** | Same core columns as the current prototype (`project_key`, `name`, `lead_user_id`, `next_issue_number`, `archived`, `archived_at`, recycle-bin fields). `next_issue_number` always starts at 1 with no admin override at creation, D95. |
| `project_key_aliases` | **Kept, unchanged** | Already implemented, D38/D90/D91. |
| `project_members` | **Kept, unchanged** | Already implemented (`project_id`, `user_id`, `role_key`). `role_key` is one of a **fixed** small enum (e.g. `admin`/`member`/`viewer`) rather than a foreign key into a configurable roles table, D3. |
| `project_roles` | **Removed as a configurable table** | Roles are a fixed application-level enum, not admin-editable rows, D3. |
| `project_role_members` | **Removed** | Superseded by the existing `project_members.role_key` column, D3. |
| `permission_schemes` | **Removed** | Replaced entirely by fixed project roles, D3. |
| `permission_grants` | **Removed** | Depends on `permission_schemes`. |
| `project_permission_schemes` | **Removed** | Depends on `permission_schemes`. |
| `project_components` | **Kept, unchanged** | Name, description, lead, default assignee; at most one per issue, D19. |

## D. Issue types, workflow, and custom fields

| Table | V1 status | Notes |
|---|---|---|
| `issue_types` | **Kept, unchanged** | Fixed 5 rows (Epic/Story/Task/Bug/Sub-task), already implemented, D29. |
| `priorities` | **Kept, unchanged** | Fixed 5 rows, already implemented, D27. |
| `resolutions` | **Kept, unchanged** | Fixed 5 rows, D28. |
| `statuses` | **Kept, but hardcoded** | A small fixed set of rows (e.g. To Do / In Progress / Done, or a few more within those categories) seeded once and never admin-editable, D4/D71. |
| `workflows` | **Removed** | One workflow is built into the application, not data-driven, D4. |
| `workflow_versions` | **Removed** | No draft/publish cycle, D4/D73. |
| `workflow_version_statuses` | **Removed** | Depends on `workflow_versions`. |
| `workflow_transitions` | **Removed or hardcoded in application code** | The fixed set of allowed status transitions lives in application logic, not a configurable table, D4. |
| `transition_conditions` | **Removed** | No configurable conditions, D74. |
| `transition_validators` | **Removed** | The one surviving rule (parent blocked from Done while sub-tasks are open) is hardcoded application logic, not a data-driven validator, D68/D75. |
| `transition_post_functions` | **Removed** | The surviving fixed behaviors (clear resolution on reopen, write `issue_history_entries`) are hardcoded, not a configurable chain, D69/D70/D76. |
| `transition_fields` | **Removed** | Transition forms (e.g. "Done requires Resolution") are hardcoded per fixed transition, D77. |
| `workflow_schemes` | **Removed** | No per-project/per-type workflow mapping — every project uses the one fixed workflow, D4. |
| `workflow_scheme_mappings` | **Removed** | Depends on `workflow_schemes`. |
| `project_workflow_schemes` | **Removed** | Depends on `workflow_schemes`. |
| `workflow_publication_jobs` | **Removed** | No publish step exists, D73. |
| `custom_fields` | **Removed** | No custom fields in V1 at all, D9. |
| `custom_field_options` | **Removed** | Depends on `custom_fields`. |
| `custom_field_contexts` | **Removed** | Depends on `custom_fields`. |
| `custom_field_context_projects` | **Removed** | Depends on `custom_fields`. |
| `custom_field_context_issue_types` | **Removed** | Depends on `custom_fields`. |
| `field_layouts` | **Removed** | No dynamic layouts; the fixed field set has one hardcoded layout, D9. |
| `field_layout_items` | **Removed** | Depends on `field_layouts`. |
| `project_field_layouts` | **Removed** | Depends on `field_layouts`. |

## E. Issues and collaboration

| Table | V1 status | Notes |
|---|---|---|
| `issues` | **Kept, simplified** | Same core columns as the current prototype (identity, content, classification, people, hierarchy, `story_points`, `due_date`, `rank_value`, lifecycle, `version`, recycle-bin fields). `rank_value` becomes a plain integer with renumber-on-insert rather than a LexoRank string, D31. No original/remaining time-estimate columns (D12), no Fix/Affects Version references (D18), no custom-field values (D9). |
| `issue_key_aliases` | **Kept, unchanged** | Already implemented. |
| `issue_custom_values` | **Removed** | Depends on `custom_fields`, D9. |
| `labels` / `issue_labels` | **Kept, unchanged** | Already implemented, D97. |
| `issue_link_types` | **Removed as a configurable table** | Link types are a fixed enum on `issue_links.link_type`, D17. |
| `issue_links` | **Kept, simplified** | `link_type` is a fixed enum (`blocks`, `relates_to`, `duplicates`, `clones`) instead of a foreign key into an admin-editable catalog, D17. |
| `issue_watchers` | **Kept, simplified** | Self watch/unwatch only; no "added by" actor tracking needed since only the watcher themself can add/remove, D20. |
| `issue_votes` | **Kept, unchanged** | One vote per user, D79. |
| `issue_history_entries` | **Kept, unchanged** | Already partly implemented; continues to record structured field changes. |
| `comments` | **Kept, simplified** | Adds an `edited_at` nullable timestamp instead of a separate version-history table, D81. |
| `comment_versions` | **Removed** | Superseded by the `edited_at` flag on `comments`, D81. |
| `comment_reactions` | **Kept, unchanged** | Fixed emoji set, D84. |
| `worklogs` | **Kept, simplified** | `time_spent`, `comment`, `work_date`, `author_user_id`. No `remaining_estimate_adjustment` column (no time estimates exist, D12) and no separate own-vs-others permission columns (any project member can edit any worklog on an issue they can access), D13. |

## F. Versions, filters, and boards

| Table | V1 status | Notes |
|---|---|---|
| `project_versions` | **Removed** | No versions/releases concept in V1, D18. |
| `issue_fix_versions` | **Removed** | Depends on `project_versions`. |
| `issue_affects_versions` | **Removed** | Depends on `project_versions`. |
| `saved_filters` | **Removed** | Filters are ad-hoc/in-UI only, never persisted, D10. |
| `filter_conditions` | **Removed** | Depends on `saved_filters`. |
| `filter_shares` | **Removed** | Depends on `saved_filters`. |
| `boards` | **Kept, drastically simplified** | One row auto-created per project (`project_id`, `name`). No `saved_filter_id` (D10), no multi-project boards (D7). |
| `board_administrators` | **Removed** | No board-level admin configuration beyond the project's own roles, D34/D35. |
| `board_columns` | **Kept, simplified** | One row per fixed workflow status (`status_id`, `wip_limit` nullable, `sort_order`) — 1:1 mapping is hardcoded, not admin-configurable, D32/D33. |
| `board_quick_filters` | **Removed** | Depends on saved filters, D35. |
| `sprints` | **Removed** | No Scrum, D7/D30. |
| `sprint_issues` | **Removed** | Depends on `sprints`. |
| `sprint_scope_events` | **Removed** | Depends on `sprints`. |
| `board_rank_rebalance_jobs` | **Removed** | Rank renumbering happens synchronously inline on insert, not as a background job — there is no job queue, D31/D51. |

## G. Attachments

| Table | V1 status | Notes |
|---|---|---|
| `attachments` | **Kept, simplified** | `storage_key` always points into the local filesystem store; there is no `storage_backend` discriminator column since only one backend exists (not even as a nullable "future S3" hook), D15. SHA-256 and size are recorded at upload. |
| `attachment_integrity_runs` | **Removed** | No periodic background integrity audit — verification happens once, at upload time, D105. |

## H. Notifications and mail (all removed or drastically cut)

| Table | V1 status | Notes |
|---|---|---|
| `notification_schemes` | **Removed** | Fixed, non-configurable notification rules, D14. |
| `notification_rules` | **Removed** | Depends on `notification_schemes`. |
| `project_notification_schemes` | **Removed** | Depends on `notification_schemes`. |
| `notifications` | **Kept, simplified** | In-app only (`user_id`, `type`, `issue_id` nullable, `read_at`). No delivery-channel column since email doesn't exist, D14. |
| `notification_preferences` | **Removed** | No per-user event/channel configuration, D86. |
| `notification_digests` | **Removed** | No digest delivery, D86. |
| `outbound_messages` | **Removed** | No outbound email backend, D52. |
| `inbound_mail_accounts` | **Removed** | No inbound email, D115. |
| `inbound_messages` | **Removed** | No inbound email, D115. |
| `inbound_message_attachments` | **Removed** | No inbound email, D119. |
| `inbound_mail_quarantine` | **Removed** | No inbound email, D122. |
| `inbound_mail_dead_letters` | **Removed** | No inbound email, D123. |

## I. Integrations, automation, templates (all removed)

| Table | V1 status | Notes |
|---|---|---|
| `webhook_subscriptions` | **Removed** | No webhooks, D39/D41. |
| `webhook_events` | **Removed** | Depends on webhooks. |
| `webhook_deliveries` | **Removed** | Depends on webhooks. |
| `external_apps` | **Removed** | Depends on webhooks/service accounts, D49. |
| `external_app_panels` | **Removed** | Depends on `external_apps`. |
| `development_links` | **Removed** | No Git integration mechanism (depends on the removed public API/webhook surface), D42. |
| `automation_rules` | **Removed** | No automation engine, D25. |
| `automation_conditions` | **Removed** | Depends on `automation_rules`. |
| `automation_actions` | **Removed** | Depends on `automation_rules`. |
| `automation_runs` | **Removed** | Depends on `automation_rules`. |
| `issue_templates` | **Removed** | No templates in V1, D61. |
| `issue_template_fields` | **Removed** | Depends on `issue_templates`. |
| `issue_template_subtasks` | **Removed** | Depends on `issue_templates`. |
| `issue_template_links` | **Removed** | Depends on `issue_templates`. |
| `issue_template_watchers` | **Removed** | Depends on `issue_templates`. |

## J. Import/export, backup, branding, updates

| Table | V1 status | Notes |
|---|---|---|
| `data_exports` | **Removed as a persisted entity** | CSV export of issues is a synchronous read-only endpoint response, not a tracked async job (there is no job queue), D48/D51. |
| `data_imports` | **Removed** | No CSV import, D48. |
| `import_mappings` | **Removed** | No Jira migration tool, D48. |
| `migration_reports` | **Removed** | No Jira migration tool, D48. |
| `restore_operations` | **Removed as a persisted entity** | `ticket-hub restore` is a synchronous CLI operation with console output, not a tracked async job, D108/D51. |
| `installation_branding` | **Removed** | No installation branding, D46. |
| `release_update_state` | **Kept, minimal** | A single small row/settings entry tracking the last-checked version, used to drive the in-app admin banner, D112. |

## Summary

Of the ~90 tables in the original target catalog, V1 keeps roughly **35**, most of them already
implemented or nearly so in the 0.2.0 prototype (`docs/SCHEMA.md`), and several of those simplified
(fixed enums instead of configurable catalogs, inline flags instead of history tables). The remaining
~55 are either removed permanently (workflow engine, custom fields, permission/notification schemes,
inbound/outbound email, webhooks, automation, templates, versions, saved filters, sprints, jobs/events/
cache/secrets infrastructure) or deferred for a later milestone with no schema commitment yet.

This is small enough that the whole V1 schema fits comfortably in the existing migration-file pattern
(`migrations/<backend>/NNN_description.sql`) without needing the module split the full target catalog
would eventually require.
