using System.Text.RegularExpressions;
using Pkgsrv.Application.Abstractions.Sources;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Sources;

public sealed class ConfiguredSourceAllowlist : ISourceAllowlist
{
    private static readonly Regex HostPattern = new(
        "^\\*\\.[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$|^[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private readonly IReadOnlyList<string> _allowedHosts;

    public ConfiguredSourceAllowlist(IEnumerable<string> allowedHosts)
    {
        ArgumentNullException.ThrowIfNull(allowedHosts);

        _allowedHosts = allowedHosts
            .Where(host => !string.IsNullOrWhiteSpace(host))
            .Select(host => host.Trim().ToLowerInvariant())
            .Distinct(StringComparer.Ordinal)
            .ToArray();

        foreach (var host in _allowedHosts)
        {
            if (!HostPattern.IsMatch(host))
            {
                throw new ArgumentException(
                    $"Source allowlist entry '{host}' is not a valid host or wildcard host.",
                    nameof(allowedHosts));
            }
        }
    }

    public IReadOnlyList<string> AllowedHosts => _allowedHosts;

    public bool IsAllowed(PackageSource source)
    {
        ArgumentNullException.ThrowIfNull(source);

        var host = source.Host.ToLowerInvariant();

        return _allowedHosts.Any(entry =>
            entry.StartsWith("*.", StringComparison.Ordinal)
                ? host.EndsWith(entry[1..], StringComparison.Ordinal)
                : host.Equals(entry, StringComparison.Ordinal));
    }
}
