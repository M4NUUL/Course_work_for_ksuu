-- Временная таблица для импорта CSV
CREATE TABLE IF NOT EXISTS staging_threats (
    threat_code  TEXT,
    title        TEXT,
    description  TEXT,
    consequences TEXT,
    source       TEXT
);

-- На всякий случай чистим перед каждым импортом
TRUNCATE staging_threats;
