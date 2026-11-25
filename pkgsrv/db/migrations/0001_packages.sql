BEGIN;

CREATE SCHEMA IF NOT EXISTS pkgsrv;

CREATE TABLE IF NOT EXISTS pkgsrv.schema_versions (
    version integer PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS pkgsrv.packages (
    name text NOT NULL,
    version text NOT NULL,
    version_major integer NOT NULL,
    version_minor integer NOT NULL,
    version_patch integer NOT NULL,
    version_pre_release text NULL,
    version_build_metadata text NULL,
    source_url text NOT NULL,
    source_commit text NOT NULL,
    toolchain_name text NOT NULL,
    toolchain_version text NOT NULL,
    content_hash text NOT NULL,
    provenance text NOT NULL,
    signature_status smallint NOT NULL,
    signature_algorithm text NULL,
    signature_value text NULL,
    isolation smallint NOT NULL,
    allow_network boolean NOT NULL,
    timeout_ticks bigint NOT NULL,
    state smallint NOT NULL,
    registered_at timestamptz NOT NULL,
    build_requested_at timestamptz NULL,
    CONSTRAINT packages_pkey PRIMARY KEY (name, version),
    CONSTRAINT packages_signature_status_valid CHECK (signature_status BETWEEN 0 AND 2),
    CONSTRAINT packages_signature_consistent CHECK (
        (signature_status = 2 AND signature_algorithm IS NOT NULL AND signature_value IS NOT NULL)
        OR (signature_status IN (0, 1) AND signature_algorithm IS NULL AND signature_value IS NULL)
    ),
    CONSTRAINT packages_isolation_valid CHECK (isolation BETWEEN 0 AND 2),
    CONSTRAINT packages_state_valid CHECK (state BETWEEN 0 AND 1),
    CONSTRAINT packages_timeout_valid CHECK (timeout_ticks BETWEEN 10000000 AND 36000000000),
    CONSTRAINT packages_build_requested_consistent CHECK (
        (state = 0 AND build_requested_at IS NULL)
        OR (state = 1 AND build_requested_at IS NOT NULL)
    )
);

CREATE INDEX IF NOT EXISTS packages_list_idx
    ON pkgsrv.packages (
        name,
        version_major,
        version_minor,
        version_patch,
        version_pre_release,
        version_build_metadata,
        version
    );

CREATE INDEX IF NOT EXISTS packages_name_lower_idx
    ON pkgsrv.packages (lower(name) text_pattern_ops);

CREATE INDEX IF NOT EXISTS packages_source_commit_idx
    ON pkgsrv.packages (source_commit text_pattern_ops);

INSERT INTO pkgsrv.schema_versions (version)
VALUES (1)
ON CONFLICT (version) DO NOTHING;

COMMIT;
