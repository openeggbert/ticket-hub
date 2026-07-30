# Artifact provenance

This package was assembled from the artifacts created during the Ticket Hub design and implementation conversation.

## Included source generations

### Original prototype

`archives/ticket-hub-original-prototype.zip`

This is the first functional vertical slice. It contains the initial Crow/vanilla UI, PostgreSQL and SQLite adapters, projects, issues, comments, simple Kanban UI, migrations, tests, and the early documentation that still contained unresolved product questions.

### Version 0.2.0 before final handoff additions

`archives/ticket-hub-0.2.0-pre-handoff.zip`

SHA-256 originally reported when created:

`a5f3669db447ebb64e979b4cc6d185a539125f60cc9bf2eec8d157606e6cef42`

This version added the approved product baseline, checksummed ordered migrations, migration locking, CLI, issue optimistic locking foundation, aliases, recycle-bin foundations, normalization, and expanded tests.

### Current working tree

`project/`

This is a copy of 0.2.0 with additional continuation-only documentation:

- `CLAUDE.md`
- `docs/PRODUCT_DECISIONS_COMPLETE.md`
- `docs/HANDOFF_NOTES.md`

No functional source behavior was intentionally changed during final packaging.

## Conversation preservation

The original chat UI is not embedded verbatim. Instead, all product choices are reconstructed in numbered form in `project/docs/PRODUCT_DECISIONS_COMPLETE.md`, and the approved behavior is consolidated thematically in `project/SPECIFICATION.md`. Together they preserve the information needed to continue without the chat.
