using System;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Pkgsrv.Infrastructure.Persistence.Postgres.Migrations
{
    /// <inheritdoc />
    public partial class AddPackageOutbox : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.CreateTable(
                name: "package_outbox",
                columns: table => new
                {
                    IdempotencyKey = table.Column<string>(type: "text", nullable: false),
                    PackageName = table.Column<string>(type: "text", nullable: false),
                    PackageVersion = table.Column<string>(type: "text", nullable: false),
                    EventType = table.Column<string>(type: "text", nullable: false),
                    Payload = table.Column<string>(type: "text", nullable: false),
                    OccurredAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    AvailableAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    ClaimedAt = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true),
                    ClaimId = table.Column<string>(type: "text", nullable: true),
                    AttemptCount = table.Column<int>(type: "integer", nullable: false),
                    Status = table.Column<int>(type: "integer", nullable: false),
                    LastError = table.Column<string>(type: "text", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_package_outbox", x => x.IdempotencyKey);
                });

            migrationBuilder.CreateIndex(
                name: "IX_package_outbox_PackageName",
                table: "package_outbox",
                column: "PackageName");

            migrationBuilder.CreateIndex(
                name: "IX_package_outbox_Status_AvailableAt",
                table: "package_outbox",
                columns: new[] { "Status", "AvailableAt" });
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "package_outbox");
        }
    }
}
