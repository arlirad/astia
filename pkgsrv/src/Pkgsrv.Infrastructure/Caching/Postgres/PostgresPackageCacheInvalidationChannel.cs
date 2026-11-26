using System.Text.Json;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Domain.Errors;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Caching.Postgres;

public static class PostgresPackageCacheInvalidationChannel
{
    public const string ChannelName = "pkgsrv_package_cache_invalidation";
    public const string ListenCommand = "LISTEN \"" + ChannelName + "\"";
    public const string NotifyCommand = "SELECT pg_notify(@channel, @payload)";
    public const int MaxPayloadLength = 8000;

    private const string AllScope = "all";
    private const string ListScope = "list";
    private const string PackageScope = "package";

    private static readonly JsonSerializerOptions SerializerOptions =
        new(JsonSerializerDefaults.Web);

    public static string Serialize(PackageCacheInvalidation invalidation)
    {
        ArgumentNullException.ThrowIfNull(invalidation);

        var payload = new Dictionary<string, string>
        {
            ["scope"] = invalidation.Scope switch
            {
                PackageCacheInvalidationScope.All => AllScope,
                PackageCacheInvalidationScope.List => ListScope,
                PackageCacheInvalidationScope.Package => PackageScope,
                _ => throw new ArgumentOutOfRangeException(
                    nameof(invalidation),
                    invalidation.Scope,
                    "Unknown package cache invalidation scope.")
            }
        };

        if (invalidation.Scope == PackageCacheInvalidationScope.Package)
        {
            var package = invalidation.Package
                ?? throw new ArgumentException(
                    "A package-scoped invalidation requires a package key.",
                    nameof(invalidation));

            payload["name"] = package.Name.Value;
            payload["version"] = package.Version.ToString();
        }

        var serialized = JsonSerializer.Serialize(payload, SerializerOptions);

        if (serialized.Length > MaxPayloadLength)
        {
            throw new ArgumentException(
                "Package cache invalidation payload exceeds the PostgreSQL notification limit.",
                nameof(invalidation));
        }

        return serialized;
    }

    public static bool TryParse(
        string? payload,
        out PackageCacheInvalidation invalidation)
    {
        invalidation = PackageCacheInvalidation.All;

        if (string.IsNullOrWhiteSpace(payload) || payload.Length > MaxPayloadLength)
        {
            return false;
        }

        try
        {
            using var document = JsonDocument.Parse(payload);
            var root = document.RootElement;

            if (root.ValueKind != JsonValueKind.Object
                || !root.TryGetProperty("scope", out var scopeElement)
                || scopeElement.ValueKind != JsonValueKind.String)
            {
                return false;
            }

            switch (scopeElement.GetString())
            {
                case AllScope:
                    invalidation = PackageCacheInvalidation.All;
                    return true;
                case ListScope:
                    invalidation = PackageCacheInvalidation.List;
                    return true;
                case PackageScope:
                    var name = ReadString(root, "name");
                    var version = ReadString(root, "version");

                    if (string.IsNullOrWhiteSpace(name)
                        || string.IsNullOrWhiteSpace(version))
                    {
                        return false;
                    }

                    invalidation = PackageCacheInvalidation.ForPackage(
                        PackageKey.Create(name, version));
                    return true;
                default:
                    return false;
            }
        }
        catch (Exception exception) when (
            exception is JsonException
                or DomainException
                or ArgumentException)
        {
            return false;
        }
    }

    private static string? ReadString(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var element)
            && element.ValueKind == JsonValueKind.String
                ? element.GetString()
                : null;
}
