using System.Net;
using System.Text.RegularExpressions;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Sources;

public sealed class ConfiguredSourceAllowlist : ISourceAllowlist
{
    private static readonly Regex HostPattern = new(
        "^\\*\\.[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$|^[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private static readonly Regex EntryPattern = new(
        "^(?<host>\\*\\.[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?|[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?)(?::(?<port>[0-9]{1,5}))?$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private readonly IReadOnlyList<AllowlistEntry> _entries;

    public ConfiguredSourceAllowlist(IEnumerable<string> allowedHosts)
    {
        ArgumentNullException.ThrowIfNull(allowedHosts);

        _entries = allowedHosts
            .Where(host => !string.IsNullOrWhiteSpace(host))
            .Select(ParseEntry)
            .Distinct()
            .ToArray();
    }

    public IReadOnlyList<string> AllowedHosts => _entries.Select(entry => entry.ToString()).ToArray();

    public bool IsAllowed(PackageSource source)
    {
        ArgumentNullException.ThrowIfNull(source);

        var host = source.Host.ToLowerInvariant();
        var port = source.RepositoryUri.Port;
        var isIpLiteral = IPAddress.TryParse(host, out _);

        foreach (var entry in _entries)
        {
            if (entry.Port is null)
            {
                if (port != 443)
                {
                    continue;
                }
            }
            else if (entry.Port != port)
            {
                continue;
            }

            if (entry.Wildcard)
            {
                if (!isIpLiteral
                    && host.EndsWith(entry.Host[1..], StringComparison.Ordinal))
                {
                    return true;
                }

                continue;
            }

            if (host.Equals(entry.Host, StringComparison.Ordinal))
            {
                return true;
            }
        }

        return false;
    }

    private static AllowlistEntry ParseEntry(string value)
    {
        var normalized = value.Trim().ToLowerInvariant();
        var match = EntryPattern.Match(normalized);

        if (!match.Success)
        {
            throw new ArgumentException(
                $"Source allowlist entry '{value}' is not a valid host, wildcard host, or host:port.",
                nameof(value));
        }

        int? port = null;

        if (match.Groups["port"].Success)
        {
            var parsed = int.Parse(match.Groups["port"].Value);

            if (parsed is < 1 or > 65535)
            {
                throw new ArgumentException(
                    $"Source allowlist entry '{value}' has an invalid port.",
                    nameof(value));
            }

            port = parsed;
        }

        var host = match.Groups["host"].Value;

        if (!HostPattern.IsMatch(host))
        {
            throw new ArgumentException(
                $"Source allowlist entry '{value}' is not a valid host or wildcard host.",
                nameof(value));
        }

        return new AllowlistEntry(host, port, host.StartsWith("*.", StringComparison.Ordinal));
    }

    private sealed record AllowlistEntry(string Host, int? Port, bool Wildcard)
    {
        public override string ToString() => Port is null ? Host : $"{Host}:{Port}";
    }
}
