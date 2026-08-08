-- Native full-text index for Ticket Hub's unbounded ticket search. The
-- contentless FTS table remains synchronized through triggers so the normal
-- tickets table stays the single source of truth. It deliberately indexes
-- only the three fields the existing LIKE search covered: key, summary and
-- description.
CREATE VIRTUAL TABLE IF NOT EXISTS ticket_search USING fts5(
    ticket_key,
    summary,
    description,
    content='tickets',
    content_rowid='rowid',
    tokenize='unicode61'
);

INSERT INTO ticket_search(rowid, ticket_key, summary, description)
SELECT rowid, ticket_key, summary, description FROM tickets;

CREATE TRIGGER IF NOT EXISTS tickets_search_after_insert
AFTER INSERT ON tickets BEGIN
    INSERT INTO ticket_search(rowid, ticket_key, summary, description)
    VALUES (new.rowid, new.ticket_key, new.summary, new.description);
END;

CREATE TRIGGER IF NOT EXISTS tickets_search_after_delete
AFTER DELETE ON tickets BEGIN
    INSERT INTO ticket_search(ticket_search, rowid, ticket_key, summary, description)
    VALUES ('delete', old.rowid, old.ticket_key, old.summary, old.description);
END;

CREATE TRIGGER IF NOT EXISTS tickets_search_after_update
AFTER UPDATE OF ticket_key, summary, description ON tickets BEGIN
    INSERT INTO ticket_search(ticket_search, rowid, ticket_key, summary, description)
    VALUES ('delete', old.rowid, old.ticket_key, old.summary, old.description);
    INSERT INTO ticket_search(rowid, ticket_key, summary, description)
    VALUES (new.rowid, new.ticket_key, new.summary, new.description);
END;
