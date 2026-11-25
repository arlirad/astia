namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresPackageSchema
{
    public const string CreateSql = """
        CREATE TABLE IF NOT EXISTS packages (
            "Name" text NOT NULL,
            "Version" text NOT NULL,
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
            "TimeoutSeconds" integer NOT NULL,
            "State" integer NOT NULL,
            "PendingEventsJson" text NOT NULL DEFAULT '[]',
            "RegisteredAt" timestamp with time zone NOT NULL,
            "BuildRequestedAt" timestamp with time zone NULL,
            CONSTRAINT "PK_packages" PRIMARY KEY ("Name", "Version")
        );

        ALTER TABLE packages
            ADD COLUMN IF NOT EXISTS "PendingEventsJson" text NOT NULL DEFAULT '[]';

        CREATE INDEX IF NOT EXISTS "IX_packages_Name" ON packages ("Name");
        CREATE INDEX IF NOT EXISTS "IX_packages_State" ON packages ("State");
        CREATE INDEX IF NOT EXISTS "IX_packages_RegisteredAt" ON packages ("RegisteredAt");
        """;
}
