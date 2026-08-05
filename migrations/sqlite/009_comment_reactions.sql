-- Fixed emoji reactions on comments (D84): a small closed set of reaction
-- keys (GitHub's well-known eight-reaction set, since the decision register
-- calls for "a fixed reaction set" without enumerating one) -- each user may
-- add each reaction at most once per comment, tracked as a three-column
-- composite primary key mirroring the existing issue_watchers/issue_votes
-- many-to-many tables (see IDatabase::addCommentReaction).

CREATE TABLE IF NOT EXISTS comment_reactions (
    comment_id TEXT NOT NULL REFERENCES comments(id) ON DELETE CASCADE,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    reaction_key TEXT NOT NULL CHECK (reaction_key IN (
        'thumbs_up', 'thumbs_down', 'laugh', 'hooray', 'confused', 'heart', 'rocket', 'eyes'
    )),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (comment_id, user_id, reaction_key)
);
