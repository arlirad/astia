using System.Globalization;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Caching.Memory;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Presentation.Contracts;

namespace Pkgsrv.Presentation.Caching;

public sealed class PackageJsonCache : IPackageJsonCache
{
    private const string AllListKey = "all";
    private const string SearchListKeyPrefix = "search:";

    private static readonly JsonSerializerOptions SerializerOptions =
        new(JsonSerializerDefaults.Web);

    private readonly IMemoryCache _cache;
    private readonly PackageJsonCacheOptions _options;
    private long _generation;

    public PackageJsonCache(IMemoryCache cache, PackageJsonCacheOptions options)
    {
        _cache = cache;
        _options = options;
    }

    public Task<string> GetListJsonAsync(
        AuthorizationContext actor,
        string? search,
        Func<CancellationToken, Task<PackageListResponse>> load,
        CancellationToken cancellationToken)
    {
        var generation = CurrentGeneration();
        var key = $"pkgsrv:json:{generation}:list:{ActorKey(actor)}:{NormalizeSearch(search)}";

        return GetOrCreateAsync(key, generation, load, cancellationToken);
    }

    public Task<string> GetPackageJsonAsync(
        AuthorizationContext actor,
        string name,
        string version,
        Func<CancellationToken, Task<PackageResponse>> load,
        CancellationToken cancellationToken)
    {
        var generation = CurrentGeneration();
        var key = $"pkgsrv:json:{generation}:package:{ActorKey(actor)}:{name}@{version}";

        return GetOrCreateAsync(key, generation, load, cancellationToken);
    }

    public void InvalidateAfterMutation()
    {
        Interlocked.Increment(ref _generation);
    }

    private async Task<string> GetOrCreateAsync<TResponse>(
        string key,
        long generation,
        Func<CancellationToken, Task<TResponse>> load,
        CancellationToken cancellationToken)
    {
        if (_cache.TryGetValue(key, out string? cached) && cached is not null)
        {
            return cached;
        }

        var response = await load(cancellationToken).ConfigureAwait(false);
        var json = JsonSerializer.Serialize(response, SerializerOptions);

        if (CurrentGeneration() == generation)
        {
            _cache.Set(
                key,
                json,
                new MemoryCacheEntryOptions
                {
                    Size = 1,
                    AbsoluteExpirationRelativeToNow = _options.Ttl
                });
        }

        return json;
    }

    private long CurrentGeneration() => Interlocked.Read(ref _generation);

    private static string NormalizeSearch(string? search)
    {
        var normalized = search?.Trim();

        return string.IsNullOrEmpty(normalized)
            ? AllListKey
            : SearchListKeyPrefix + normalized.ToLowerInvariant();
    }

    private static string ActorKey(AuthorizationContext actor)
    {
        var roles = actor.Roles
            .OrderBy(role => role, StringComparer.Ordinal)
            .ToArray();
        var builder = new StringBuilder();

        AppendPart(builder, actor.ActorId);
        AppendPart(builder, actor.AccountId?.ToString("N") ?? string.Empty);
        AppendPart(builder, roles.Length.ToString(CultureInfo.InvariantCulture));

        foreach (var role in roles)
        {
            AppendPart(builder, role);
        }

        return builder.ToString();
    }

    private static void AppendPart(StringBuilder builder, string value)
    {
        builder.Append(value.Length.ToString(CultureInfo.InvariantCulture));
        builder.Append(':');
        builder.Append(value);
    }
}
