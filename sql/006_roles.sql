-- sql/006_roles.sql
BEGIN;

ALTER TABLE users
ADD COLUMN IF NOT EXISTS role TEXT;

-- Заполняем role по существующему is_admin: если is_admin true -> admin, иначе viewer
UPDATE users
SET role = CASE WHEN is_admin THEN 'admin' ELSE 'viewer' END
WHERE role IS NULL OR role = '';

-- Дефолт и NOT NULL
ALTER TABLE users
ALTER COLUMN role SET DEFAULT 'viewer';

UPDATE users SET role = 'viewer' WHERE role IS NULL OR role = '';
ALTER TABLE users ALTER COLUMN role SET NOT NULL;

-- Ограничение на значения роли
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint WHERE conname = 'users_role_check'
    ) THEN
        ALTER TABLE users
        ADD CONSTRAINT users_role_check CHECK (role IN ('admin','editor','viewer'));
    END IF;
END$$;

COMMIT;
