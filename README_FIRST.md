# Ticket Hub — complete handoff archive

Created: 2026-07-30  
Purpose: preserve everything needed to continue development after the defining ChatGPT conversation is deleted.

## Start here

1. Read `handoff/CLAUDE_CODE_HANDOFF.md`.
2. Read `project/CLAUDE.md`.
3. Read `project/SPECIFICATION.md` and `project/docs/PRODUCT_DECISIONS_COMPLETE.md`.
4. Read `project/NEXT.md`, `project/docs/ARCHITECTURE.md`, `project/docs/DATA_MODEL.md`, and `project/docs/ROADMAP.md`.
5. Build and run the existing tests before changing code.

## Archive layout

- `project/` — the current Ticket Hub 0.2.0 source tree, plus handoff documents added for the next agent.
- `handoff/` — high-level continuation instructions, current status, provenance, and a ready-to-paste Claude Code prompt.
- `archives/ticket-hub-original-prototype.zip` — the first generated functional prototype before the 0.2.0 foundation work.
- `archives/ticket-hub-0.2.0-pre-handoff.zip` — the exact 0.2.0 archive produced before this final packaging pass.
- `SHA256SUMS.txt` — checksums for every file in this handoff archive.

## Important truthfulness rule

The product specification describes the approved target. It does **not** mean all features are implemented. The authoritative current implementation status is in:

- `project/README.md`
- `project/docs/SCHEMA.md`
- `project/docs/SCOPE.md`
- `project/docs/VERIFICATION.md`
- `handoff/IMPLEMENTATION_STATE.md`

## Product identity

- Name: Ticket Hub
- Main C++ namespace: `TicketHub`
- Stack: C++20, Crow, vanilla HTML/CSS/JavaScript
- Primary database: PostgreSQL
- Optional database: SQLite with the same user-facing feature set but single-instance operational limits
- License: MIT
