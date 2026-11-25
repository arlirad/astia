using Npgsql;
using NpgsqlTypes;

namespace Pkgsrv.Infrastructure.Postgres;

public static class PostgresPackageSearch
{
    public static void BindParameters(NpgsqlCommand command, string? search, int maxResults)
    {
        ArgumentNullException.ThrowIfNull(command);

        var normalized = string.IsNullOrWhiteSpace(search) ? null : search.Trim();
        var escaped = normalized is null ? null : EscapeLike(normalized);
        var contains = escaped is null ? null : $"%{escaped}%";
        var prefix = escaped is null ? null : $"{escaped}%";

        AddNullableText(command, "contains", contains);
        AddNullableText(command, "prefix", prefix);
        command.Parameters.Add(
            new NpgsqlParameter("limit", NpgsqlDbType.Integer)
            {
                Value = Math.Clamp(maxResults, 1, PostgresPackageStoreOptions.MaximumListResults)
            });
    }

    public static string EscapeLike(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("%", "\\%", StringComparison.Ordinal)
            .Replace("_", "\\_", StringComparison.Ordinal);
    }

    private static void AddNullableText(NpgsqlCommand command, string name, string? value) =>
        command.Parameters.Add(
            new NpgsqlParameter(name, NpgsqlDbType.Text)
            {
                Value = (object?)value ?? DBNull.Value
            });
}
