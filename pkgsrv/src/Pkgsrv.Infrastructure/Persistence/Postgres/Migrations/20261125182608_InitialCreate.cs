using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Pkgsrv.Infrastructure.Persistence.Postgres.Migrations
{
    /// <inheritdoc />
    public partial class InitialCreate : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql(
                """
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
                    "TimeoutTicks" bigint NOT NULL,
                    "State" integer NOT NULL,
                    "PendingEventsJson" text NOT NULL DEFAULT '[]',
                    "RegisteredAt" timestamp with time zone NOT NULL,
                    "BuildRequestedAt" timestamp with time zone NULL,
                    CONSTRAINT "PK_packages" PRIMARY KEY ("Name", "Version"),
                    CONSTRAINT "CK_packages_TimeoutTicks" CHECK ("TimeoutTicks" BETWEEN 10000000 AND 36000000000)
                );

                ALTER TABLE packages
                    ADD COLUMN IF NOT EXISTS "PendingEventsJson" text NOT NULL DEFAULT '[]';

                ALTER TABLE packages
                    ADD COLUMN IF NOT EXISTS "TimeoutTicks" bigint NOT NULL DEFAULT 0;

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
                """);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql("DROP TABLE IF EXISTS packages;");
        }
    }
}
