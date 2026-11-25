using System.Collections.Concurrent;
using System.Text.Json;
using Pkgsrv.Application.Abstractions.Caching;
using Pkgsrv.Application.Abstractions.Time;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Caching;

public sealed class JsonPackageReadModelCache : IPackageReadModelCache
{
    private const string DetailPrefix = "detail:";
    private const string ListPrefix = "list:";
    private const string AllListKey = ListPrefix + "all";
    private const string SearchListPrefix = ListPrefix + "search:";

    private static readonly JsonSerializerOptions SerializerOptions = new(JsonSerializerDefaults.Web);

    private readonly ConcurrentDictionary<string, CacheEntry> _entries = new(StringComparer.Ordinal);
    private readonly PackageReadModelCacheOptions _options;
    private readonly ISystemClock _clock;
    private readonly object _mutationGate = new();
    private long _accessSequence;
    private long _generation;

    public JsonPackageReadModelCache(
        PackageReadModelCacheOptions options,
        ISystemClock clock)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(clock);

        _options = options;
        _clock = clock;
    }

    public long CaptureGeneration() => Interlocked.Read(ref _generation);

    public PackageDetailsDto? GetDetails(PackageKey key)
    {
        ArgumentNullException.ThrowIfNull(key);
        return Get<PackageDetailsDto>(DetailKey(key));
    }

    public void SetDetails(PackageDetailsDto package, long generation)
    {
        ArgumentNullException.ThrowIfNull(package);
        Set(DetailKey(PackageKey.Create(package.Name, package.Version)), package, generation);
    }

    public IReadOnlyList<PackageSummaryDto>? GetList(string? search) =>
        Get<PackageSummaryDto[]>(ListKey(search));

    public void SetList(string? search, IReadOnlyList<PackageSummaryDto> packages, long generation)
    {
        ArgumentNullException.ThrowIfNull(packages);
        Set(ListKey(search), packages.ToArray(), generation);
    }

    public void InvalidatePackage(PackageKey key)
    {
        ArgumentNullException.ThrowIfNull(key);

        lock (_mutationGate)
        {
            Interlocked.Increment(ref _generation);
            _entries.TryRemove(DetailKey(key), out _);
            RemoveListEntries();
        }
    }

    public void InvalidateLists()
    {
        lock (_mutationGate)
        {
            Interlocked.Increment(ref _generation);
            RemoveListEntries();
        }
    }

    private T? Get<T>(string key)
        where T : class
    {
        if (!_entries.TryGetValue(key, out var entry))
        {
            return null;
        }

        lock (entry)
        {
            var now = _clock.UtcNow;

            if (now >= entry.AbsoluteExpiresAt || now >= entry.SlidingExpiresAt)
            {
                _entries.TryRemove(key, out _);
                return null;
            }

            var slidingExpiresAt = now + _options.SlidingExpiration;
            entry.SlidingExpiresAt = slidingExpiresAt < entry.AbsoluteExpiresAt
                ? slidingExpiresAt
                : entry.AbsoluteExpiresAt;
            entry.LastAccessSequence = Interlocked.Increment(ref _accessSequence);

            try
            {
                return JsonSerializer.Deserialize<T>(entry.Json, SerializerOptions);
            }
            catch (JsonException)
            {
                _entries.TryRemove(key, out _);
                return null;
            }
        }
    }

    private void Set<T>(string key, T value, long generation)
        where T : class
    {
        var now = _clock.UtcNow;
        var absoluteExpiresAt = now + _options.AbsoluteExpiration;
        var slidingExpiresAt = now + _options.SlidingExpiration;
        var entry = new CacheEntry(
            JsonSerializer.Serialize(value, SerializerOptions),
            absoluteExpiresAt,
            slidingExpiresAt < absoluteExpiresAt ? slidingExpiresAt : absoluteExpiresAt,
            Interlocked.Increment(ref _accessSequence));

        lock (_mutationGate)
        {
            if (generation != Interlocked.Read(ref _generation))
            {
                return;
            }

            _entries[key] = entry;
            TrimToBounds(now);
        }
    }

    private void RemoveListEntries()
    {
        foreach (var key in _entries.Keys)
        {
            if (key.StartsWith(ListPrefix, StringComparison.Ordinal))
            {
                _entries.TryRemove(key, out _);
            }
        }
    }

    private void TrimToBounds(DateTimeOffset now)
    {
        if (_entries.Count <= _options.MaxEntries)
        {
            return;
        }

        foreach (var pair in _entries)
        {
            if (now >= pair.Value.AbsoluteExpiresAt || now >= pair.Value.SlidingExpiresAt)
            {
                _entries.TryRemove(pair.Key, out _);
            }
        }

        while (_entries.Count > _options.MaxEntries)
        {
            var oldest = _entries
                .OrderBy(pair => pair.Value.LastAccessSequence)
                .FirstOrDefault();

            if (oldest.Key is null)
            {
                break;
            }

            _entries.TryRemove(oldest.Key, out _);
        }
    }

    private static string DetailKey(PackageKey key) =>
        DetailPrefix + key.Name.Value + "@" + key.Version;

    private static string ListKey(string? search)
    {
        var normalized = search?.Trim();

        return string.IsNullOrEmpty(normalized)
            ? AllListKey
            : SearchListPrefix + normalized.ToLowerInvariant();
    }

    private sealed class CacheEntry
    {
        public CacheEntry(
            string json,
            DateTimeOffset absoluteExpiresAt,
            DateTimeOffset slidingExpiresAt,
            long lastAccessSequence)
        {
            Json = json;
            AbsoluteExpiresAt = absoluteExpiresAt;
            SlidingExpiresAt = slidingExpiresAt;
            LastAccessSequence = lastAccessSequence;
        }

        public string Json { get; }

        public DateTimeOffset AbsoluteExpiresAt { get; }

        public DateTimeOffset SlidingExpiresAt { get; set; }

        public long LastAccessSequence { get; set; }
    }
}
