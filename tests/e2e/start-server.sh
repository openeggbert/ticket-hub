#!/usr/bin/env bash
set -euo pipefail

# Each Playwright invocation gets an isolated SQLite database. The test server
# seeds the documented demo users and has no outbound SMTP configuration.
E2E_DATABASE="${TMPDIR:-/tmp}/ticket-hub-e2e-${RANDOM}-${RANDOM}.sqlite3"
exec env \
  TICKETHUB_DB_DRIVER=sqlite \
  TICKETHUB_SQLITE_PATH="$E2E_DATABASE" \
  TICKETHUB_WEB_ROOT="$PWD/web" \
  TICKETHUB_MIGRATIONS_ROOT="$PWD/migrations" \
  TICKETHUB_ATTACHMENTS_DIR="${TMPDIR:-/tmp}/ticket-hub-e2e-attachments-${RANDOM}-${RANDOM}" \
  TICKETHUB_BIND_ADDRESS=127.0.0.1 \
  TICKETHUB_PORT=4173 \
  TICKETHUB_AUTO_MIGRATE=true \
  TICKETHUB_SEED_DEMO=true \
  ./build/e2e/ticket-hub
