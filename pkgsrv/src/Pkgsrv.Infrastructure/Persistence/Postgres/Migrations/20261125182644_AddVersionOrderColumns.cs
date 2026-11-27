using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Pkgsrv.Infrastructure.Persistence.Postgres.Migrations
{
    /// <inheritdoc />
    public partial class AddVersionOrderColumns : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql(
                """
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

                CREATE INDEX IF NOT EXISTS "IX_packages_VersionOrder" ON packages (
                    "Name",
                    "VersionMajor",
                    "VersionMinor",
                    "VersionPatch",
                    "VersionPreRelease",
                    "VersionBuildMetadata",
                    "Version"
                );
                """);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql(
                """
                DROP INDEX IF EXISTS "IX_packages_VersionOrder";

                ALTER TABLE packages DROP COLUMN IF EXISTS "VersionBuildMetadata";
                ALTER TABLE packages DROP COLUMN IF EXISTS "VersionMajor";
                ALTER TABLE packages DROP COLUMN IF EXISTS "VersionMinor";
                ALTER TABLE packages DROP COLUMN IF EXISTS "VersionPatch";
                ALTER TABLE packages DROP COLUMN IF EXISTS "VersionPreRelease";
                """);
        }
    }
}
