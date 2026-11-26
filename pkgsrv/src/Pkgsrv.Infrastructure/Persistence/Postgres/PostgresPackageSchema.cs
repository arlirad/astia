namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresPackageSchema
{
    public const int RequiredVersion = 3;

    public static readonly string[] RequiredColumns =
    [
        "Name",
        "Version",
        "VersionMajor",
        "VersionMinor",
        "VersionPatch",
        "VersionPreRelease",
        "VersionBuildMetadata",
        "SourceUrl",
        "SourceCommit",
        "ToolchainName",
        "ToolchainVersion",
        "ContentHash",
        "Provenance",
        "SignatureStatus",
        "SignatureAlgorithm",
        "SignatureValue",
        "BuildIsolation",
        "AllowNetwork",
        "TimeoutTicks",
        "State",
        "PendingEventsJson",
        "RegisteredAt",
        "BuildRequestedAt",
        "PublishedAt"
    ];

    public const string CreateSql = """
        CREATE TABLE IF NOT EXISTS schema_versions (
            version integer PRIMARY KEY,
            applied_at timestamp with time zone NOT NULL DEFAULT now()
        );

        CREATE TABLE IF NOT EXISTS packages (
            "Name" text NOT NULL,
            "Version" text NOT NULL,
            "VersionMajor" integer NOT NULL,
            "VersionMinor" integer NOT NULL,
            "VersionPatch" integer NOT NULL,
            "VersionPreRelease" text NULL,
            "VersionBuildMetadata" text NULL,
            "SourceUrl" text NOT NULL,
            "SourceCommit" text NOT NULL,
            "ToolchainName" text NOT NULL,
            "ToolchainVersion" text NOT NULL,
            "ContentHash" text NOT NULL,
            "Provenance" text NOT NULL,
            "SignatureStatus" integer NOT NULL,
            "SignatureAlgorithm" text NULL,
            "SignatureValue" text NULL,
            "BuildIsolation" integer NOT NULL,
            "AllowNetwork" boolean NOT NULL,
            "TimeoutTicks" bigint NOT NULL,
            "State" integer NOT NULL,
            "PendingEventsJson" text NOT NULL DEFAULT '[]',
            "RegisteredAt" timestamp with time zone NOT NULL,
            "BuildRequestedAt" timestamp with time zone NULL,
            "PublishedAt" timestamp with time zone NULL,
            CONSTRAINT "PK_packages" PRIMARY KEY ("Name", "Version"),
            CONSTRAINT "CK_packages_TimeoutTicks" CHECK ("TimeoutTicks" BETWEEN 10000000 AND 36000000000)
        );

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "PendingEventsJson" text NOT NULL DEFAULT '[]';

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "PublishedAt" timestamp with time zone NULL;

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "TimeoutTicks" bigint NOT NULL DEFAULT 0;

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "VersionMajor" integer NOT NULL DEFAULT 0;

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "VersionMinor" integer NOT NULL DEFAULT 0;

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "VersionPatch" integer NOT NULL DEFAULT 0;

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "VersionPreRelease" text NULL;

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "VersionBuildMetadata" text NULL;

        DO $pkgsrv$
        BEGIN
            UPDATE packages p
            SET
                "VersionMajor" = COALESCE(parsed.parts[1]::integer, 0),
                "VersionMinor" = COALESCE(parsed.parts[2]::integer, 0),
                "VersionPatch" = COALESCE(parsed.parts[3]::integer, 0),
                "VersionPreRelease" = parsed.parts[4],
                "VersionBuildMetadata" = parsed.parts[5]
            FROM (
                SELECT
                    "Name",
                    "Version",
                    regexp_match(
                        "Version",
                        '^([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$'
                    ) AS parts
                FROM packages
                WHERE "VersionMajor" = 0
                  AND "VersionMinor" = 0
                  AND "VersionPatch" = 0
            ) AS parsed
            WHERE p."Name" = parsed."Name"
              AND p."Version" = parsed."Version"
              AND parsed.parts IS NOT NULL;
        END
        $pkgsrv$;

        DO $pkgsrv$
        BEGIN
            IF EXISTS (
                SELECT 1
                FROM information_schema.columns
                WHERE table_schema = current_schema()
                  AND table_name = 'packages'
                  AND column_name = 'TimeoutSeconds'
            ) THEN
                UPDATE packages
                SET "TimeoutTicks" = "TimeoutSeconds"::bigint * 10000000
                WHERE "TimeoutTicks" = 0;
            END IF;
        END
        $pkgsrv$;

        CREATE INDEX IF NOT EXISTS "IX_packages_Name" ON packages ("Name");
        CREATE INDEX IF NOT EXISTS "IX_packages_State" ON packages ("State");
        CREATE INDEX IF NOT EXISTS "IX_packages_RegisteredAt" ON packages ("RegisteredAt");
        CREATE INDEX IF NOT EXISTS "IX_packages_VersionOrder" ON packages (
            "Name",
            "VersionMajor",
            "VersionMinor",
            "VersionPatch",
            "VersionPreRelease",
            "VersionBuildMetadata",
            "Version"
        );

        INSERT INTO schema_versions (version)
        VALUES (1), (2), (3)
        ON CONFLICT (version) DO NOTHING;
        """;

    public static readonly string ValidationSql = BuildValidationSql();

    private static string BuildValidationSql()
    {
        var requiredValues = string.Join(
            ",\n",
            RequiredColumns.Select(column => $"                ('{column}')"));

        return $$"""
            SELECT
                to_regclass('packages') IS NOT NULL,
                COALESCE((SELECT MAX(version) FROM schema_versions), 0),
                COALESCE((
                    SELECT string_agg(required.column_name, ',' ORDER BY required.column_name)
                    FROM (VALUES
            {{requiredValues}}
                    ) AS required(column_name)
                    WHERE NOT EXISTS (
                        SELECT 1
                        FROM information_schema.columns c
                        WHERE c.table_schema = current_schema()
                          AND c.table_name = 'packages'
                          AND c.column_name = required.column_name
                    )
                ), '')
            """;
    }
}
