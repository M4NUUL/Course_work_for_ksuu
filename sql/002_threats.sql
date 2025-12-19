CREATE TABLE IF NOT EXISTS threats (
    threat_code  TEXT PRIMARY KEY,   -- УБИ.001
    title        TEXT NOT NULL,
    description  TEXT,
    consequences TEXT,
    source       TEXT,

    created_by   BIGINT REFERENCES users(id),
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_by   BIGINT REFERENCES users(id),
    updated_at   TIMESTAMPTZ
);
