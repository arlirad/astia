using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Pkgsrv.Infrastructure.Persistence.Postgres.Migrations
{
    /// <inheritdoc />
    public partial class AddPublicationTimestamp : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql(
                """
                ALTER TABLE packages
                    ADD COLUMN IF NOT EXISTS "PublishedAt" timestamp with time zone NULL;
                """);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.Sql(
                """
                ALTER TABLE packages DROP COLUMN IF EXISTS "PublishedAt";
                """);
        }
    }
}
