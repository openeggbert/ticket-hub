#!/bin/sh
set -eu

# Compose injects this separately as well as into the libpq connection string.
# Refuse the empty/example value early and clearly rather than letting either
# PostgreSQL or the application fail later with a misleading auth error.
if [ -z "${TICKETHUB_POSTGRES_PASSWORD:-}" ] || [ "${TICKETHUB_POSTGRES_PASSWORD}" = "replace-with-a-long-random-secret" ]; then
  echo "TICKETHUB_POSTGRES_PASSWORD must be set to a real secret before starting Ticket Hub." >&2
  exit 64
fi

exec "$@"
