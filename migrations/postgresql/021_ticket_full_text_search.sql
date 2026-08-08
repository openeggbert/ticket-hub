-- Native PostgreSQL full-text index for Ticket Hub's unbounded ticket search.
-- A generated vector keeps the application query parameterized and lets the
-- database maintain its GIN index as tickets are created or edited.
ALTER TABLE tickets
    ADD COLUMN IF NOT EXISTS search_vector tsvector GENERATED ALWAYS AS (
        to_tsvector('simple', coalesce(ticket_key, '') || ' ' || coalesce(summary, '') || ' ' || coalesce(description, ''))
    ) STORED;

CREATE INDEX IF NOT EXISTS idx_tickets_search_vector ON tickets USING GIN(search_vector);
