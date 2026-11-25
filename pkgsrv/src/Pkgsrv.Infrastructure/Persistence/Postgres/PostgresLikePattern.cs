namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public static class PostgresLikePattern
{
    public static string Escape(string value)
    {
        ArgumentNullException.ThrowIfNull(value);

        return value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("%", "\\%", StringComparison.Ordinal)
            .Replace("_", "\\_", StringComparison.Ordinal);
    }

    public static string Contains(string value) => $"%{Escape(value)}%";

    public static string Prefix(string value) => $"{Escape(value)}%";
}
