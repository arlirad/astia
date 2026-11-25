using Npgsql;
using Pkgsrv.Infrastructure.Postgres;

namespace Pkgsrv.Infrastructure.Tests;

public sealed class PostgresPackageSqlTests
{
    [Fact]
    public void Search_UsesBoundParametersDeterministicOrderingAndBoundedLimit()
    {
        var sql = PostgresPackageSql.Search;

        Assert.Contains("@contains", sql, StringComparison.Ordinal);
        Assert.Contains("@prefix", sql, StringComparison.Ordinal);
        Assert.Contains("@limit", sql, StringComparison.Ordinal);
        Assert.Contains("ILIKE @contains ESCAPE '\\'", sql, StringComparison.Ordinal);
        Assert.Contains("ORDER BY", sql, StringComparison.Ordinal);
        Assert.Contains("version_pre_release COLLATE \"C\" ASC NULLS LAST", sql, StringComparison.Ordinal);
        Assert.Contains("version_build_metadata COLLATE \"C\" ASC NULLS FIRST", sql, StringComparison.Ordinal);
        Assert.Contains("LIMIT @limit", sql, StringComparison.Ordinal);
        Assert.DoesNotContain("widget", sql, StringComparison.Ordinal);
    }

    [Fact]
    public void BindParameters_EscapesLikeWildcardsAndClampsLimit()
    {
        using var command = new NpgsqlCommand(PostgresPackageSql.Search);

        PostgresPackageSearch.BindParameters(command, @"50%_off\path", 50_000);

        Assert.Equal(@"%50\%\_off\\path%", command.Parameters["contains"].Value);
        Assert.Equal(@"50\%\_off\\path%", command.Parameters["prefix"].Value);
        Assert.Equal(10_000, command.Parameters["limit"].Value);
    }

    [Fact]
    public void BindParameters_UsesNullPatternsForEmptySearch()
    {
        using var command = new NpgsqlCommand(PostgresPackageSql.Search);

        PostgresPackageSearch.BindParameters(command, "   ", 100);

        Assert.Equal(DBNull.Value, command.Parameters["contains"].Value);
        Assert.Equal(DBNull.Value, command.Parameters["prefix"].Value);
        Assert.Equal(100, command.Parameters["limit"].Value);
    }

    [Fact]
    public void InsertAndUpdate_UseUniqueConflictAndRowLockSemantics()
    {
        Assert.Contains("ON CONFLICT (name, version) DO NOTHING", PostgresPackageSql.Insert, StringComparison.Ordinal);
        Assert.Contains("FOR UPDATE", PostgresPackageSql.SelectStateForUpdate, StringComparison.Ordinal);
        Assert.Contains("SET state = @state", PostgresPackageSql.UpdateState, StringComparison.Ordinal);
        Assert.Contains(
            "WHERE name = @name AND version = @version",
            PostgresPackageSql.UpdateState,
            StringComparison.Ordinal);
    }

    [Fact]
    public void MigrationScript_DefinesVersionedSchemaUniqueKeyAndSearchIndexes()
    {
        var script = File.ReadAllText(FindMigrationScript());

        Assert.Contains("CREATE TABLE IF NOT EXISTS pkgsrv.schema_versions", script, StringComparison.Ordinal);
        Assert.Contains("PRIMARY KEY (name, version)", script, StringComparison.Ordinal);
        Assert.Contains("packages_list_idx", script, StringComparison.Ordinal);
        Assert.Contains("packages_name_lower_idx", script, StringComparison.Ordinal);
        Assert.Contains("packages_source_commit_idx", script, StringComparison.Ordinal);
        Assert.Contains("timeout_ticks bigint", script, StringComparison.Ordinal);
        Assert.Contains("packages_timeout_valid", script, StringComparison.Ordinal);
        Assert.Contains("INSERT INTO pkgsrv.schema_versions (version)", script, StringComparison.Ordinal);
        Assert.DoesNotContain("DROP TABLE", script, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("TRUNCATE", script, StringComparison.OrdinalIgnoreCase);
    }

    private static string FindMigrationScript()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            var candidate = Path.Combine(directory.FullName, "db", "migrations", "0001_packages.sql");

            if (File.Exists(candidate))
            {
                return candidate;
            }

            directory = directory.Parent;
        }

        throw new FileNotFoundException("Could not locate pkgsrv/db/migrations/0001_packages.sql.");
    }
}
