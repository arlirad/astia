BEGIN;

ALTER TABLE packages
    ADD COLUMN IF NOT EXISTS "PublishedAt" timestamp with time zone NULL;

INSERT INTO schema_versions (version)
VALUES (3)
ON CONFLICT (version) DO NOTHING;

COMMIT;
