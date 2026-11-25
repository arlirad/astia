using System.Text.RegularExpressions;
using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public sealed record PackageSource
{
    private static readonly Regex CommitPattern = new(
        "^[0-9a-fA-F]{40}$",
        RegexOptions.CultureInvariant,
        TimeSpan.FromSeconds(1));

    private PackageSource(Uri repositoryUri, string commitSha)
    {
        RepositoryUri = repositoryUri;
        CommitSha = commitSha;
    }

    public Uri RepositoryUri { get; }

    public string CommitSha { get; }

    public string Host => RepositoryUri.Host;

    public static PackageSource Create(string? repositoryUrl, string? commitSha)
    {
        if (!Uri.TryCreate(repositoryUrl?.Trim(), UriKind.Absolute, out var uri)
            || uri.Scheme != Uri.UriSchemeHttps
            || string.IsNullOrWhiteSpace(uri.Host)
            || !string.IsNullOrEmpty(uri.UserInfo))
        {
            throw new DomainException(
                DomainErrorCodes.SourceUrlInvalid,
                "Source URL must be an absolute HTTPS URL without embedded credentials.");
        }

        if (string.IsNullOrWhiteSpace(commitSha) || !CommitPattern.IsMatch(commitSha.Trim()))
        {
            throw new DomainException(
                DomainErrorCodes.SourceCommitInvalid,
                "Source commit must be a 40-character hexadecimal Git commit SHA.");
        }

        return new PackageSource(uri, commitSha.Trim().ToLowerInvariant());
    }
}
