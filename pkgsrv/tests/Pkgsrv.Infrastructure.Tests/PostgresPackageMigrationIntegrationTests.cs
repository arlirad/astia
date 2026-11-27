using Microsoft.EntityFrameworkCore;
using Npgsql;
using Pkgsrv.Infrastructure.Packages;
using Pkgsrv.Infrastructure.Persistence.Postgres;
using Pkgsrv.TestSupport;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageMigrationIntegrationTests
{
    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task FreshDatabase_IsCreatedByEfMigrationChain()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        await using var context = database.CreateContext();

        await context.Database.MigrateAsync(CancellationToken.None);

        var applied = (await context.Database.GetAppliedMigrationsAsync(CancellationToken.None))
            .ToArray();
        Assert.Equal(5, applied.Length);
        Assert.EndsWith("_InitialCreate", applied[0], StringComparison.Ordinal);
        Assert.EndsWith("_AddVersionOrderColumns", applied[1], StringComparison.Ordinal);
        Assert.EndsWith("_AddPublicationTimestamp", applied[2], StringComparison.Ordinal);
        Assert.EndsWith("_AddPackageOutbox", applied[3], StringComparison.Ordinal);
        Assert.EndsWith("_AddSemVerPrecedenceKey", applied[4], StringComparison.Ordinal);
        Assert.Equal(
            5L,
            await context.Database
                .SqlQueryRaw<long>("SELECT COUNT(*) AS \"Value\" FROM \"__EFMigrationsHistory\"")
                .SingleAsync(CancellationToken.None));

        var package = TestPackages.CreatePackage(name: $"fresh-{Guid.NewGuid():N}");
        context.Packages.Add(PostgresPackageMapper.FromDomain(package));
        await context.SaveChangesAsync(CancellationToken.None);

        var record = await context.Packages
            .AsNoTracking()
            .SingleAsync(candidate => candidate.Name == package.Key.Name.Value, CancellationToken.None);

        Assert.Equal(package.Manifest.Version.ToString(), record.Version);
        Assert.Equal(
            PackageVersionOrder.CreatePreReleaseSortKey(package.Manifest.Version.PreRelease),
            record.VersionPreReleaseSortKey);
        Assert.Equal(package.BuildPolicy.Timeout.Ticks, record.TimeoutTicks);
    }

    [PostgresFact]
    [Trait("Category", "Postgres")]
    public async Task OldCustomV1Database_UpgradesWithoutDataLoss()
    {
        await using var database = await PostgresTestDatabase.CreateAsync(CancellationToken.None);
        var name = $"legacy-{Guid.NewGuid():N}";

        await CreateOldCustomV1PackageAsync(database.ConnectionString, name, CancellationToken.None);

        await using var context = database.CreateContext();
        await context.Database.MigrateAsync(CancellationToken.None);

        var applied = (await context.Database.GetAppliedMigrationsAsync(CancellationToken.None))
            .ToArray();
        Assert.Equal(5, applied.Length);
        Assert.Equal(
            5L,
            await context.Database
                .SqlQueryRaw<long>("SELECT COUNT(*) AS \"Value\" FROM \"__EFMigrationsHistory\"")
                .SingleAsync(CancellationToken.None));

        var record = await context.Packages
            .AsNoTracking()
            .SingleAsync(candidate => candidate.Name == name, CancellationToken.None);

        Assert.Equal("1.2.3-rc.1+build.7", record.Version);
        Assert.Equal(1, record.VersionMajor);
        Assert.Equal(2, record.VersionMinor);
        Assert.Equal(3, record.VersionPatch);
        Assert.Equal("rc.1", record.VersionPreRelease);
        Assert.Equal("build.7", record.VersionBuildMetadata);
        Assert.Equal(
            PackageVersionOrder.CreatePreReleaseSortKey("rc.1"),
            record.VersionPreReleaseSortKey);
        Assert.Equal(300L * 10_000_000, record.TimeoutTicks);
        Assert.Equal("[]", record.PendingEventsJson);
        Assert.Null(record.PublishedAt);
        Assert.Equal(TestPackages.SourceUrlValue, record.SourceUrl);
        Assert.Equal(TestPackages.CommitSha, record.SourceCommit);

        Assert.Equal(
            1L,
            await context.Database
                .SqlQueryRaw<long>("SELECT COUNT(*) AS \"Value\" FROM schema_versions")
                .SingleAsync(CancellationToken.None));
    }

    private static async Task CreateOldCustomV1PackageAsync(
        string connectionString,
        string name,
        CancellationToken cancellationToken)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        await connection.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using (var schema = new NpgsqlCommand(
            """
            CREATE TABLE schema_versions (
                version integer PRIMARY KEY,
                applied_at timestamp with time zone NOT NULL DEFAULT now()
            );
            INSERT INTO schema_versions (version) VALUES (1);

            CREATE TABLE packages (
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
                "RegisteredAt" timestamp with time zone NOT NULL,
                "BuildRequestedAt" timestamp with time zone NULL,
                CONSTRAINT "PK_packages" PRIMARY KEY ("Name", "Version")
            );
            """,
            connection))
        {
            await schema.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        }

        await using var insert = new NpgsqlCommand(
            """
            INSERT INTO packages (
                "Name", "Version", "SourceUrl", "SourceCommit",
                "ToolchainName", "ToolchainVersion", "ContentHash", "Provenance",
                "SignatureStatus", "SignatureAlgorithm", "SignatureValue",
                "BuildIsolation", "AllowNetwork", "TimeoutSeconds", "State",
                "RegisteredAt", "BuildRequestedAt"
            )
            VALUES (
                @name, @version, @sourceUrl, @sourceCommit,
                @toolchainName, @toolchainVersion, @contentHash, @provenance,
                @signatureStatus, NULL, NULL,
                @buildIsolation, @allowNetwork, @timeoutSeconds, @state,
                @registeredAt, NULL
            );
            """,
            connection);

        insert.Parameters.AddWithValue("name", name);
        insert.Parameters.AddWithValue("version", "1.2.3-rc.1+build.7");
        insert.Parameters.AddWithValue("sourceUrl", TestPackages.SourceUrlValue);
        insert.Parameters.AddWithValue("sourceCommit", TestPackages.CommitSha);
        insert.Parameters.AddWithValue("toolchainName", "gcc");
        insert.Parameters.AddWithValue("toolchainVersion", "13.2.0");
        insert.Parameters.AddWithValue("contentHash", TestPackages.ContentHashValue);
        insert.Parameters.AddWithValue("provenance", "ci://pkgsrv/tests/legacy");
        insert.Parameters.AddWithValue("signatureStatus", 1);
        insert.Parameters.AddWithValue("buildIsolation", 1);
        insert.Parameters.AddWithValue("allowNetwork", false);
        insert.Parameters.AddWithValue("timeoutSeconds", 300);
        insert.Parameters.AddWithValue("state", 0);
        insert.Parameters.AddWithValue("registeredAt", TestPackages.Timestamp);

        await insert.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }
}
