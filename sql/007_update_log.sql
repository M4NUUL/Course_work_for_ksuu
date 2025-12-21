-- sql/007_update_log.sql
CREATE TABLE IF NOT EXISTS update_log (
  id           BIGSERIAL PRIMARY KEY,
  user_id      BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  imported_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  inserted     INT NOT NULL,
  updated      INT NOT NULL,
  source       TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_update_log_imported_at ON update_log(imported_at DESC);
CREATE INDEX IF NOT EXISTS idx_update_log_user_id ON update_log(user_id);
