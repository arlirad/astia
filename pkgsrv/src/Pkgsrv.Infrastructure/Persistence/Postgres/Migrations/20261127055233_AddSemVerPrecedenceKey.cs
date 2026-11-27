using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Pkgsrv.Infrastructure.Persistence.Postgres.Migrations
{
    /// <inheritdoc />
    public partial class AddSemVerPrecedenceKey : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropIndex(
                name: "IX_packages_VersionOrder",
                table: "packages");

            migrationBuilder.AddColumn<string>(
                name: "VersionPreReleaseSortKey",
                table: "packages",
                type: "text",
                nullable: false,
                defaultValue: "");

            migrationBuilder.Sql(
                """
                DO $pkgsrv$
                DECLARE
                    package_row record;
                    identifiers text[];
                    identifier_index integer;
                    part text;
                    sort_key text;
                BEGIN
                    FOR package_row IN
                        SELECT "Name", "Version", "VersionPreRelease" FROM packages
                    LOOP
                        IF package_row."VersionPreRelease" IS NULL THEN
                            sort_key := chr(4);
                        ELSE
                            identifiers := string_to_array(
                                package_row."VersionPreRelease",
                                '.');

                            IF identifiers IS NULL OR cardinality(identifiers) = 0 THEN
                                sort_key := chr(3);
                            ELSE
                                sort_key := '';

                                FOR identifier_index IN 1 .. cardinality(identifiers)
                                LOOP
                                    part := identifiers[identifier_index];

                                    IF identifier_index > 1 THEN
                                        sort_key := sort_key || chr(1);
                                    END IF;

                                    IF part ~ '^(0|[1-9][0-9]*)$' THEN
                                        sort_key := sort_key
                                            || chr(2)
                                            || repeat(chr(2), length(part))
                                            || chr(1)
                                            || part;
                                    ELSE
                                        sort_key := sort_key || chr(3) || part;
                                    END IF;
                                END LOOP;
                            END IF;
                        END IF;

                        UPDATE packages
                        SET "VersionPreReleaseSortKey" = sort_key
                        WHERE "Name" = package_row."Name"
                          AND "Version" = package_row."Version";
                    END LOOP;
                END
                $pkgsrv$;
                """);

            migrationBuilder.CreateIndex(
                name: "IX_packages_VersionOrder",
                table: "packages",
                columns: new[] { "Name", "VersionMajor", "VersionMinor", "VersionPatch", "VersionPreReleaseSortKey", "Version" });
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropIndex(
                name: "IX_packages_VersionOrder",
                table: "packages");

            migrationBuilder.DropColumn(
                name: "VersionPreReleaseSortKey",
                table: "packages");

            migrationBuilder.CreateIndex(
                name: "IX_packages_VersionOrder",
                table: "packages",
                columns: new[] { "Name", "VersionMajor", "VersionMinor", "VersionPatch", "VersionPreRelease", "VersionBuildMetadata", "Version" });
        }
    }
}
