# Changelog

## 0.2.0 — product baseline and migration foundation

- Consolidated the approved Jira-like product specification.
- Added architecture, target data model and phased roadmap documents.
- Added ordered schema migration discovery and checksum verification.
- Added PostgreSQL advisory locking for migrations.
- Separated demo seed execution from schema migration history.
- Added issue version to support optimistic status-change conflicts.
- Added project/issue key-alias tables and alias lookup for issues.
- Added recycle-bin foundation columns for projects, issues, comments and attachments.
- Added normalized project/issue key and label handling.
- Fixed duplicate project rows in the SQLite adapter.
- Expanded migration, domain and SQLite integration tests.
