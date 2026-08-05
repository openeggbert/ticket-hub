-- Simple integer manual ordering with renumbering on reorder (D31),
-- replacing the LexoRank-style string rank the prototype anticipated but
-- never implemented or read/wrote anywhere. `rank_value` was added in
-- 003_product_foundation.sql and is dropped here instead of edited in
-- place, since applied migrations are immutable.
ALTER TABLE issues DROP COLUMN rank_value;
ALTER TABLE issues ADD COLUMN rank_order INTEGER NOT NULL DEFAULT 0;

-- Seed a stable initial order: creation order within each project.
UPDATE issues SET rank_order = issue_number;
