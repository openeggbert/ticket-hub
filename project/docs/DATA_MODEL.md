# Target logical data model

This is the target engine-neutral catalog for the approved Ticket Hub product. It is not a claim that every table is already implemented. `docs/SCHEMA.md` describes the current physical prototype schema.

PostgreSQL and SQLite receive separate migrations. Logical UUIDs are application-generated. PostgreSQL stores timestamps as `TIMESTAMPTZ`; SQLite stores canonical UTC text. Date-only values remain date-only.

## Conventions

Unless stated otherwise, mutable entity tables contain:

- `id` UUID primary key,
- `created_at` timestamp,
- `updated_at` timestamp,
- `version` positive integer for optimistic locking where concurrent editing matters.

Soft-deletable records add `deleted_at` and `deleted_by_user_id`. User references normally use `ON DELETE SET NULL` or survive through anonymization rather than physical user deletion.

Structured rule/configuration payloads may use validated JSON, but identity, ownership, ordering, joins and searchable fields remain relational columns.

## A. System and operations

### `schema_migrations`

| Column | Type | Rules |
|---|---|---|
| `version` | string(128) | PK, ordered migration identifier |
| `checksum` | string(64) | required after adoption |
| `applied_at` | timestamp | required |

### `installation_settings`

| Column | Type | Rules |
|---|---|---|
| `setting_key` | string(160) | PK |
| `value_json` | JSON | validated by setting schema |
| `is_secret_reference` | bool | default false |
| `updated_by_user_id` | UUID nullable | audit actor |
| `updated_at` | timestamp | required |

### `secret_references`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `name` | string(160) | unique |
| `backend_type` | string(40) | env, file, database-encrypted, kubernetes, vault… |
| `locator` | text | backend-specific non-secret locator |
| `encrypted_value` | binary/text nullable | only for encrypted-database backend |
| `key_version` | string nullable | rotation metadata |
| `created_at`, `updated_at` | timestamp | required |

### `idempotency_keys`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `principal_type` | string(30) | user, service_account |
| `principal_id` | UUID | indexed |
| `operation_key` | string(160) | endpoint/use-case identity |
| `idempotency_key` | string(255) | caller value |
| `request_hash` | string(64) | detects conflicting reuse |
| `response_status` | integer nullable | completed response |
| `response_body` | text/JSON nullable | bounded stored response |
| `state` | string(20) | processing, completed, failed |
| `expires_at` | timestamp | required |
| `created_at`, `updated_at` | timestamp | required |

Unique: (`principal_type`, `principal_id`, `operation_key`, `idempotency_key`).

### `durable_events`

| Column | Type | Rules |
|---|---|---|
| `event_sequence` | 64-bit integer | PK, monotonic |
| `event_id` | UUID | unique |
| `event_type` | string(120) | indexed |
| `aggregate_type` | string(80) | issue, project, sprint… |
| `aggregate_id` | UUID nullable | indexed |
| `payload_json` | JSON | versioned payload |
| `correlation_id` | string(80) nullable | trace correlation |
| `created_at` | timestamp | indexed |

### `event_consumers`

| Column | Type | Rules |
|---|---|---|
| `consumer_key` | string(160) | PK |
| `last_event_sequence` | 64-bit integer | required |
| `updated_at` | timestamp | required |

### `background_jobs`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `job_type` | string(120) | indexed |
| `payload_json` | JSON | validated per job type |
| `state` | string(20) | queued, running, succeeded, retry, dead |
| `priority` | integer | default 0 |
| `available_at` | timestamp | indexed claim time |
| `attempt_count` | integer | non-negative |
| `max_attempts` | integer | positive |
| `locked_by` | string nullable | worker identity |
| `locked_at` | timestamp nullable | lease start |
| `last_error` | text nullable | redacted |
| `correlation_id` | string nullable | trace link |
| `created_at`, `updated_at`, `completed_at` | timestamp | completed nullable |

### `job_attempts`

`id`, `job_id`, `attempt_number`, `worker_id`, `started_at`, `finished_at`, `outcome`, `error_summary`.

### `audit_events`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `category` | string(80) | indexed, retention category |
| `action` | string(120) | indexed |
| `actor_type` | string(30) | user, service, system |
| `actor_id` | UUID nullable | indexed |
| `target_type` | string(80) nullable | indexed |
| `target_id` | UUID nullable | indexed |
| `project_id` | UUID nullable | indexed |
| `ip_address` | string nullable | security events |
| `correlation_id` | string nullable | trace link |
| `before_json` | JSON nullable | redacted structured state |
| `after_json` | JSON nullable | redacted structured state |
| `metadata_json` | JSON | details |
| `created_at` | timestamp | indexed |

### `audit_retention_policies`

`category` PK, `retention_days` nullable, `export_before_delete` bool, `updated_by_user_id`, `updated_at`.

## B. Identity and authentication

### `users`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK, immutable |
| `email` | string(320) | unique canonical email |
| `handle` | string(80) nullable | unique when present |
| `display_name` | string(160) | required |
| `avatar_url` | text nullable | internal/external avatar |
| `locale` | string(35) | default `en` |
| `time_zone` | string(80) | IANA zone |
| `clock_format` | string(8) | 12h or 24h |
| `active` | bool | login allowed |
| `anonymized_at` | timestamp nullable | privacy lifecycle |
| `merged_into_user_id` | UUID nullable | surviving account |
| `created_at`, `updated_at` | timestamp | required |

### `local_credentials`

`user_id` PK/FK, `password_hash`, `hash_algorithm`, `hash_parameters_json`, `password_changed_at`, `failed_attempts`, `locked_until`, `must_change_password`, `updated_at`.

### `password_reset_tokens`

`id`, `user_id`, `token_hash` unique, `expires_at`, `used_at`, `requested_ip`, `created_at`.

### `oidc_providers`

`id`, `provider_key` unique, `display_name`, `issuer_url`, `client_id`, `client_secret_ref_id`, `scopes`, `enabled`, `provisioning_mode`, `allowed_domains_json`, `default_groups_json`, `group_claim`, `created_at`, `updated_at`.

### `external_identities`

`id`, `user_id`, `provider_id`, `subject`, `email_at_link`, `claims_snapshot_json`, `last_login_at`, `created_at`, `updated_at`.

Unique: (`provider_id`, `subject`).

### `invitations`

`id`, `email`, `token_hash` unique, `invited_by_user_id`, `default_groups_json`, `expires_at`, `accepted_at`, `revoked_at`, `created_at`.

### `sessions`

`id`, `user_id`, `token_hash` unique, `csrf_secret_hash`, `created_ip`, `last_ip`, `user_agent`, `created_at`, `last_seen_at`, `expires_at`, `revoked_at`, `revoked_reason`.

### `groups`

`id`, `group_key` unique, `display_name`, `description`, `managed_by_oidc_provider_id` nullable, `active`, `created_at`, `updated_at`.

### `group_members`

`group_id`, `user_id`, `source` (local/OIDC), `created_at`; PK (`group_id`, `user_id`, `source`).

### `service_accounts`

`id`, `name` unique, `description`, `owner_user_id` nullable, `active`, `created_at`, `updated_at`.

### `api_tokens`

`id`, `owner_type`, `owner_id`, `name`, `token_prefix`, `token_hash` unique, `scopes_json`, `expires_at`, `last_used_at`, `last_used_ip`, `rotated_from_token_id` nullable, `revoked_at`, `created_at`.

### `account_merge_events`

`id`, `source_user_id`, `target_user_id`, `performed_by_user_id`, `summary_json`, `created_at`.

### `user_preferences`

`user_id` PK, `theme`, `locale`, `time_zone`, `clock_format`, `notification_digest_mode`, `preference_json`, `updated_at`.

## C. Projects, roles and permission schemes

### `projects`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `project_key` | string(12) | unique active/current key |
| `name` | string(160) | required |
| `description_markdown` | text | default empty |
| `lead_user_id` | UUID nullable | project lead |
| `next_issue_number` | 64-bit integer | positive, never decreases |
| `project_type` | string(30) | initially `software_company_managed` |
| `archived_at` | timestamp nullable | read-only state |
| `deleted_at` | timestamp nullable | recycle bin |
| `deleted_by_user_id` | UUID nullable | audit actor |
| `purge_after` | timestamp nullable | retention result |
| `required_export_id` | UUID nullable | successful pre-delete export |
| `created_at`, `updated_at`, `version` | timestamp/integer | required |

### `project_key_aliases`

`alias_key` string(12) PK, `project_id` UUID nullable, `created_at`. A null project reserves a key after permanent deletion.

### `project_roles`

`id`, `role_key` unique, `name`, `description`, `system_role` bool, `active`, `created_at`, `updated_at`.

### `project_role_members`

`project_id`, `role_id`, `principal_type` (user/group), `principal_id`, `created_at`; composite unique grant.

### `permission_schemes`

`id`, `name` unique, `description`, `system_default`, `created_at`, `updated_at`, `version`.

### `permission_grants`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `scheme_id` | UUID | indexed |
| `permission_key` | string(120) | browse_project, edit_issue… |
| `principal_type` | string(40) | user, group, project_role, reporter, assignee, project_lead, authenticated, anonymous |
| `principal_id` | UUID nullable | only named principals |
| `created_at` | timestamp | required |

### `project_permission_schemes`

`project_id` PK, `permission_scheme_id`, `assigned_at`, `assigned_by_user_id`.

### `project_components`

`id`, `project_id`, `name`, `description`, `lead_user_id`, `default_assignee_mode`, `default_assignee_user_id` nullable, `archived_at`, `created_at`, `updated_at`.

Unique live component name per project.

## D. Fixed catalogs and workflow

### `issue_types`

Seeded fixed rows: `epic`, `story`, `task`, `bug`, `sub-task`.

Columns: `id`, `type_key` unique, `name_key` translation key, `hierarchy_level` (1, 0, -1), `icon_key`, `sort_order`, `active`.

### `priorities`

Seeded fixed rows: highest, high, medium, low, lowest.

Columns: `id`, `priority_key` unique, `name_key`, `rank` unique, `icon_key`, `color_token`.

### `resolutions`

Seeded fixed rows: fixed, done, wont-fix, duplicate, cannot-reproduce.

Columns: `id`, `resolution_key` unique, `name_key`, `sort_order`.

### `statuses`

`id`, `status_key` unique, `name`, `description`, `category` (todo/in_progress/done), `color_token`, `active`, `created_at`, `updated_at`.

### `workflows`

`id`, `name` unique, `description`, `published_version_id` nullable, `draft_version_id` nullable, `created_at`, `updated_at`.

### `workflow_versions`

`id`, `workflow_id`, `version_number`, `state` (draft/published/retired), `based_on_version_id` nullable, `created_by_user_id`, `created_at`, `published_at`, `change_summary`.

Unique (`workflow_id`, `version_number`).

### `workflow_version_statuses`

`workflow_version_id`, `status_id`, `is_initial`, `sort_order`; PK (`workflow_version_id`, `status_id`).

### `workflow_transitions`

`id`, `workflow_version_id`, `transition_key`, `name`, `from_status_id` nullable for global transition, `to_status_id`, `sort_order`, `description`, `created_at`.

### `transition_conditions`

`id`, `transition_id`, `group_id` nullable, `boolean_operator`, `condition_type`, `configuration_json`, `sort_order`.

### `transition_validators`

`id`, `transition_id`, `validator_type`, `configuration_json`, `sort_order`, `error_message`.

### `transition_post_functions`

`id`, `transition_id`, `function_type`, `configuration_json`, `sort_order`, `system_required`, `immutable_position` nullable.

### `transition_fields`

`transition_id`, `field_key`, `sort_order`, `required`, `default_value_json`, `help_text`, `show_comment_field`; composite unique field.

### `workflow_schemes`

`id`, `name` unique, `description`, `created_at`, `updated_at`, `version`.

### `workflow_scheme_mappings`

`scheme_id`, `issue_type_id`, `workflow_id`; PK (`scheme_id`, `issue_type_id`).

### `project_workflow_schemes`

`project_id` PK, `workflow_scheme_id`, `assigned_at`, `assigned_by_user_id`.

### `workflow_publication_jobs`

`id`, `workflow_id`, `draft_version_id`, `state`, `impact_summary_json`, `migration_plan_json`, `requested_by_user_id`, `created_at`, `completed_at`, `error`.

## E. Custom fields and field layout

### `custom_fields`

`id`, `field_key` unique, `name`, `description`, `field_type`, `active`, `searchable`, `encrypted`, `created_by_user_id`, `created_at`, `updated_at`, `version`.

### `custom_field_options`

`id`, `field_id`, `parent_option_id` nullable, `value`, `sort_order`, `disabled`, `created_at`, `updated_at`.

### `custom_field_contexts`

`id`, `field_id`, `name`, `description`, `is_global`, `required`, `hidden`, `default_value_json`, `show_on_create`, `show_on_edit`, `show_on_view`, `sort_order`, `created_at`, `updated_at`.

### `custom_field_context_projects`

`context_id`, `project_id`; PK pair.

### `custom_field_context_issue_types`

`context_id`, `issue_type_id`; PK pair.

### `field_layouts`

`id`, `name`, `description`, `created_at`, `updated_at`.

### `field_layout_items`

`layout_id`, `field_key`, `sort_order`, `required_override` nullable, `hidden_override` nullable, `display_name_override` nullable; PK (`layout_id`, `field_key`).

### `project_field_layouts`

`project_id`, `issue_type_id`, `field_layout_id`; PK (`project_id`, `issue_type_id`).

## F. Issues and collaboration

### `issues`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK |
| `project_id` | UUID | required, indexed |
| `issue_number` | 64-bit integer | positive, unique per project |
| `issue_key` | string(64) | unique current key |
| `issue_type_id` | UUID | fixed catalog |
| `summary` | string(255) | required |
| `description_markdown` | text | default empty |
| `status_id` | UUID | workflow-controlled |
| `priority_id` | UUID | required |
| `resolution_id` | UUID nullable | fixed catalog |
| `reporter_user_id` | UUID | required |
| `assignee_user_id` | UUID nullable | indexed |
| `epic_issue_id` | UUID nullable | only standard issues |
| `parent_issue_id` | UUID nullable | only sub-task parent |
| `component_id` | UUID nullable | same project |
| `story_points` | decimal nullable | non-negative |
| `original_estimate_seconds` | 64-bit integer nullable | non-negative |
| `remaining_estimate_seconds` | 64-bit integer nullable | non-negative |
| `time_spent_seconds` | 64-bit integer | derived/maintained non-negative |
| `due_date` | date nullable | date-only |
| `rank_value` | string | globally ordered rank |
| `completed_at` | timestamp nullable | category transition tracking |
| `deleted_at`, `deleted_by_user_id`, `purge_after` | nullable | recycle bin |
| `created_at`, `updated_at`, `version` | required | optimistic locking |

Checks enforce issue-type hierarchy shape. Unique (`project_id`, `issue_number`).

### `issue_key_aliases`

`alias_key` string(64) PK, `issue_id` UUID nullable, `created_at`. Null issue reserves key after purge.

### `issue_custom_values`

`issue_id`, `field_id`, `context_id`, typed scalar columns (`text_value`, `number_value`, `date_value`, `timestamp_value`, `bool_value`, `user_id_value`, `project_id_value`, `issue_id_value`), `json_value` for multi-value, `encrypted_value`, `updated_at`, `version`.

Exactly one compatible value representation is populated.

### `labels`

`id`, `normalized_name` unique, `display_name`, `created_at`.

### `issue_labels`

`issue_id`, `label_id`, `created_at`; PK pair.

### `issue_link_types`

`id`, `link_key` unique, `name`, `outward_description`, `inward_description`, `active`, `created_at`, `updated_at`.

### `issue_links`

`id`, `link_type_id`, `source_issue_id`, `target_issue_id`, `created_by_user_id`, `created_at`; source differs from target; duplicate direction rule enforced.

### `issue_watchers`

`issue_id`, `user_id`, `added_by_user_id`, `created_at`; PK (`issue_id`, `user_id`).

### `issue_votes`

`issue_id`, `user_id`, `created_at`; PK (`issue_id`, `user_id`).

### `issue_history_entries`

`id`, `issue_id`, `actor_user_id` nullable, `change_group_id`, `field_key`, `old_value_json`, `new_value_json`, `event_type`, `created_at`.

### `comments`

`id`, `issue_id`, `author_user_id`, `current_version_number`, `deleted_at`, `deleted_by_user_id`, `created_at`, `updated_at`, `version`.

### `comment_versions`

`id`, `comment_id`, `version_number`, `body_markdown`, `edited_by_user_id`, `created_at`; unique (`comment_id`, `version_number`).

### `comment_reactions`

`comment_id`, `user_id`, `reaction_key`, `created_at`; PK triple. `reaction_key` is from a fixed allowed set.

### `worklogs`

`id`, `issue_id`, `author_user_id`, `work_date`, `time_spent_seconds`, `comment_markdown`, `remaining_adjustment_mode`, `remaining_estimate_seconds_after` nullable, `deleted_at`, `deleted_by_user_id`, `created_at`, `updated_at`, `version`.

## G. Versions, releases and issue membership

### `project_versions`

`id`, `project_id`, `name`, `description`, `release_date` nullable, `state` (unreleased/released/archived), `released_at`, `archived_at`, `release_notes_markdown`, `merged_into_version_id` nullable, `created_at`, `updated_at`, `version`.

### `issue_fix_versions`

`issue_id`, `version_id`, `created_at`; PK pair.

### `issue_affects_versions`

`issue_id`, `version_id`, `created_at`; PK pair.

## H. Filters, boards and sprints

### `saved_filters`

`id`, `name`, `description`, `owner_user_id`, `sort_spec_json`, `created_at`, `updated_at`, `version`.

### `filter_conditions`

`id`, `filter_id`, `parent_condition_id` nullable, `boolean_operator`, `field_key`, `operator`, `value_json`, `sort_order`.

### `filter_shares`

`filter_id`, `principal_type`, `principal_id` nullable, `permission` (view/edit), `created_at`; unique grant.

### `boards`

`id`, `name`, `board_type` (scrum/kanban), `filter_id`, `owner_user_id`, `active`, `estimation_mode`, `swimlane_mode`, `created_at`, `updated_at`, `version`.

### `board_administrators`

`board_id`, `principal_type`, `principal_id`, `created_at`; unique grant.

### `board_columns`

`id`, `board_id`, `status_id`, `name`, `sort_order`, `wip_limit` nullable; unique (`board_id`, `status_id`) enforces one status per column.

### `board_quick_filters`

`id`, `board_id`, `name`, `filter_id`, `sort_order`, `active`, `created_at`, `updated_at`.

### `sprints`

`id`, `board_id`, `name`, `goal`, `state` (planned/active/completed), `start_at`, `end_at`, `completed_at`, `created_by_user_id`, `created_at`, `updated_at`, `version`.

Constraint: at most one active sprint per Scrum board.

### `sprint_issues`

`sprint_id`, `issue_id`, `added_at`, `added_by_user_id`, `removed_at` nullable, `removed_by_user_id` nullable; history-preserving membership.

### `sprint_scope_events`

`id`, `sprint_id`, `issue_id`, `event_type`, `estimate_snapshot_json`, `actor_user_id`, `created_at`.

### `board_rank_rebalance_jobs`

`id`, `board_id` nullable, `range_start`, `range_end`, `state`, `created_at`, `completed_at`.

## I. Attachments and storage

### `attachments`

| Column | Type | Rules |
|---|---|---|
| `id` | UUID | PK and Markdown reference |
| `issue_id` | UUID | required |
| `comment_id` | UUID nullable | upload context |
| `uploader_user_id` | UUID | required |
| `file_name` | string(255) | duplicates allowed |
| `content_type_declared` | string(160) | client value |
| `content_type_detected` | string(160) | server inspection |
| `byte_size` | 64-bit integer | non-negative |
| `sha256` | string(64) | integrity checksum |
| `storage_backend` | string(40) | filesystem, s3… |
| `storage_key` | text | unique immutable object key |
| `preview_kind` | string(30) | none/image/pdf/text/audio/video |
| `integrity_state` | string(30) | verified/missing/corrupt/unknown |
| `last_verified_at` | timestamp nullable | audit time |
| `deleted_at`, `deleted_by_user_id`, `purge_after` | nullable | recycle bin |
| `created_at` | timestamp | required |

No deduplication table exists; every attachment owns one physical object.

### `attachment_integrity_runs`

`id`, `started_at`, `completed_at`, `state`, `checked_count`, `missing_count`, `corrupt_count`, `details_json`.

## J. Notifications and outbound mail

### `notification_schemes`

`id`, `name` unique, `description`, `created_at`, `updated_at`, `version`.

### `notification_rules`

`id`, `scheme_id`, `event_type`, `recipient_type`, `recipient_id` nullable, `channels_json`, `created_at`.

### `project_notification_schemes`

`project_id` PK, `notification_scheme_id`, `assigned_at`, `assigned_by_user_id`.

### `notifications`

`id`, `user_id`, `event_type`, `title`, `body_json`, `target_type`, `target_id`, `read_at`, `created_at`.

### `notification_preferences`

`user_id`, `event_type`, `channel`, `delivery_mode`, `enabled`, `updated_at`; PK triple.

### `notification_digests`

`id`, `user_id`, `period_start`, `period_end`, `state`, `payload_json`, `scheduled_at`, `sent_at`, `error`.

### `outbound_messages`

`id`, `message_type`, `recipient`, `template_key`, `locale`, `payload_json`, `state`, `provider_message_id`, `attempt_count`, `last_error`, `created_at`, `sent_at`.

## K. Inbound mail

### `inbound_mail_accounts`

`id`, `account_key` unique, `adapter_type`, `configuration_json`, `secret_ref_id` nullable, `enabled`, `poll_interval_seconds`, `last_success_at`, `created_at`, `updated_at`.

### `inbound_messages`

`id`, `account_id`, `message_id`, `provider_uid`, `normalized_checksum`, `sender_email`, `recipient_email`, `subject`, `raw_storage_key`, `state`, `target_issue_id` nullable, `created_issue_id` nullable, `created_comment_id` nullable, `attempt_count`, `last_error`, `received_at`, `processed_at`.

Unique identifiers are adapter-aware; conflicts may enter quarantine.

### `inbound_message_attachments`

`id`, `inbound_message_id`, `file_name`, `content_type`, `byte_size`, `temporary_storage_key`, `sha256`, `created_at`.

### `inbound_mail_quarantine`

`id`, `inbound_message_id`, `reason`, `details_json`, `reviewed_by_user_id` nullable, `decision` nullable, `created_at`, `reviewed_at`.

### `inbound_mail_dead_letters`

`id`, `inbound_message_id`, `final_error`, `attempt_count`, `created_at`, `retried_at` nullable, `resolved_at` nullable.

## L. Webhooks and external apps

### `webhook_subscriptions`

`id`, `name`, `target_url`, `secret_ref_id`, `active`, `event_types_json`, `project_id` nullable, `filter_id` nullable, `created_by_user_id`, `created_at`, `updated_at`, `version`.

### `webhook_events`

`id`, `subscription_id`, `event_id`, `payload_version`, `payload_json`, `created_at`.

### `webhook_deliveries`

`id`, `webhook_event_id`, `attempt_number`, `state`, `http_status` nullable, `response_excerpt` nullable, `error` nullable, `scheduled_at`, `started_at`, `completed_at`, `next_retry_at` nullable.

### `external_apps`

`id`, `app_key` unique, `name`, `description`, `service_account_id`, `manifest_json`, `active`, `installed_by_user_id`, `created_at`, `updated_at`.

### `external_app_panels`

`id`, `app_id`, `location_key`, `title`, `url_template`, `required_scopes_json`, `sort_order`, `active`.

### `development_links`

`id`, `issue_id`, `link_type` (commit/branch/pr/mr/build), `provider`, `external_id`, `url`, `title`, `state` nullable, `metadata_json`, `created_at`, `updated_at`; unique provider/external identity per issue.

## M. Automation and templates

### `automation_rules`

`id`, `name`, `project_id` nullable, `active`, `trigger_type`, `created_by_user_id`, `created_at`, `updated_at`, `version`.

### `automation_conditions`

`id`, `rule_id`, `parent_condition_id` nullable, `boolean_operator`, `condition_type`, `configuration_json`, `sort_order`.

### `automation_actions`

`id`, `rule_id`, `action_type`, `configuration_json`, `sort_order`.

### `automation_runs`

`id`, `rule_id`, `trigger_event_id`, `state`, `issue_id` nullable, `actions_summary_json`, `error`, `started_at`, `completed_at`.

### `issue_templates`

`id`, `scope_type` (global/project), `project_id` nullable, `name`, `description`, `issue_type_id`, `active`, `created_by_user_id`, `created_at`, `updated_at`, `version`.

### `issue_template_fields`

`template_id`, `field_key`, `value_json`; PK pair.

### `issue_template_subtasks`

`id`, `template_id`, `summary`, `description_markdown`, `field_values_json`, `sort_order`.

### `issue_template_links`

`id`, `template_id`, `link_type_id`, `target_issue_id`, `direction`.

### `issue_template_watchers`

`template_id`, `user_id`; PK pair.

## N. Imports, exports, backup and migration

### `data_exports`

`id`, `export_type` (installation/project/csv), `project_id` nullable, `requested_by_user_id`, `state`, `format_version`, `manifest_storage_key`, `database_storage_key`, `attachments_storage_key` nullable, `sha256`, `created_at`, `completed_at`, `expires_at`, `error`.

### `data_imports`

`id`, `import_type` (ticket-hub/jira/csv), `requested_by_user_id`, `state`, `source_storage_key`, `format_version`, `options_json`, `created_at`, `completed_at`, `error`.

### `import_mappings`

`id`, `import_id`, `entity_type`, `source_identifier`, `target_id` nullable, `mapping_state`, `details_json`.

### `migration_reports`

`id`, `import_id`, `severity`, `entity_type`, `source_identifier`, `message`, `details_json`, `created_at`.

### `restore_operations`

`id`, `requested_by_user_id`, `backup_export_id`, `state`, `temporary_environment_ref`, `verification_json`, `created_at`, `switched_at`, `error`.

## O. Branding and release awareness

### `installation_branding`

Single-row key: `installation_name`, `logo_attachment_id` nullable, `favicon_attachment_id` nullable, `accent_token`, `login_message_markdown`, `updated_by_user_id`, `updated_at`.

### `release_update_state`

Single-row key: `channel` (stable/lts/preview), `last_checked_at`, `latest_version`, `latest_release_json`, `dismissed_version`, `updated_at`.

## Critical relational constraints

1. Project key and all aliases are unique across current and historical keys.
2. Issue key and all aliases are unique across current and historical keys.
3. `issue_number` is unique per project and never reused by application policy.
4. Epic, parent, component, sprint and version relationships obey project compatibility rules.
5. Sub-tasks remain in their parent's project and sprint.
6. Only workflow transitions mutate status.
7. At most one active sprint exists per Scrum board.
8. One board column maps to exactly one status.
9. Soft-deleted issues, comments and attachments are excluded from ordinary queries.
10. Durable side effects reference committed events/jobs, never uncommitted in-memory work.
