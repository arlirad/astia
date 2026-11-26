using Microsoft.EntityFrameworkCore;
using Npgsql;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Domain.Events;
using Pkgsrv.Domain.Packages;
using Pkgsrv.Infrastructure.Packages;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PostgresPackageRepository : IPackageRepository, IPackageIndex
{
    public const int MaxListResults = PackageIndexLimits.MaxListResults;

    private readonly IDbContextFactory<PostgresPackageDbContext> _contextFactory;

    public PostgresPackageRepository(IDbContextFactory<PostgresPackageDbContext> contextFactory)
    {
        _contextFactory = contextFactory;
    }

    public async Task<Package?> FindAsync(PackageKey key, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        cancellationToken.ThrowIfCancellationRequested();

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);
        var record = await context.Packages
            .AsNoTracking()
            .SingleOrDefaultAsync(
                candidate => candidate.Name == key.Name.Value
                    && candidate.Version == key.Version.ToString(),
                cancellationToken)
            .ConfigureAwait(false);

        return record is null ? null : PostgresPackageMapper.ToDomain(record);
    }

    public async Task<bool> TryAddAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        cancellationToken.ThrowIfCancellationRequested();

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);
        context.Packages.Add(PostgresPackageMapper.FromDomain(package));

        try
        {
            await context.SaveChangesAsync(cancellationToken).ConfigureAwait(false);
            return true;
        }
        catch (DbUpdateException exception) when (IsUniqueViolation(exception))
        {
            return false;
        }
    }

    public async Task UpdateAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);
        cancellationToken.ThrowIfCancellationRequested();

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        var name = package.Key.Name.Value;
        var version = package.Key.Version.ToString();
        var expectedState = PostgresPackageStateTransition.ExpectedPersistedState(package.State);
        var sourceUrl = package.Manifest.Source.RepositoryUri.ToString();
        var sourceCommit = package.Manifest.Source.CommitSha;
        var toolchainName = package.Manifest.Toolchain.Name;
        var toolchainVersion = package.Manifest.Toolchain.Version;
        var contentHash = package.Manifest.ContentHash.Value;
        var provenance = package.Manifest.Provenance.Value;
        var signatureStatus = (int)package.Manifest.Signature.Status;
        var signatureAlgorithm = package.Manifest.Signature.Algorithm;
        var signatureValue = package.Manifest.Signature.Value;
        var buildIsolation = (int)package.BuildPolicy.Isolation;
        var allowNetwork = package.BuildPolicy.AllowNetwork;
        var timeoutTicks = package.BuildPolicy.Timeout.Ticks;
        var state = (int)package.State;
        var pendingEventsJson = PostgresPendingEventSerializer.Serialize(package.PeekDomainEvents());
        var buildRequestedAt = package.BuildRequestedAt;

        var affected = await context.Packages
            .Where(record => record.Name == name
                && record.Version == version
                && record.State == (int)expectedState)
            .ExecuteUpdateAsync(
                setters => setters
                    .SetProperty(record => record.SourceUrl, sourceUrl)
                    .SetProperty(record => record.SourceCommit, sourceCommit)
                    .SetProperty(record => record.ToolchainName, toolchainName)
                    .SetProperty(record => record.ToolchainVersion, toolchainVersion)
                    .SetProperty(record => record.ContentHash, contentHash)
                    .SetProperty(record => record.Provenance, provenance)
                    .SetProperty(record => record.SignatureStatus, signatureStatus)
                    .SetProperty(record => record.SignatureAlgorithm, signatureAlgorithm)
                    .SetProperty(record => record.SignatureValue, signatureValue)
                    .SetProperty(record => record.BuildIsolation, buildIsolation)
                    .SetProperty(record => record.AllowNetwork, allowNetwork)
                    .SetProperty(record => record.TimeoutTicks, timeoutTicks)
                    .SetProperty(record => record.State, state)
                    .SetProperty(record => record.PendingEventsJson, pendingEventsJson)
                    .SetProperty(record => record.BuildRequestedAt, buildRequestedAt),
                cancellationToken)
            .ConfigureAwait(false);

        if (affected == 0)
        {
            throw new PackageConflictException(
                package.Key.ToString(),
                $"Package {package.Key} was modified by another request; the persisted state was stale.");
        }
    }

    public async Task UpdateSignatureAsync(Package package, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);
        var name = package.Key.Name.Value;
        var version = package.Key.Version.ToString();
        var signatureStatus = (int)package.Manifest.Signature.Status;
        var signatureAlgorithm = package.Manifest.Signature.Algorithm;
        var signatureValue = package.Manifest.Signature.Value;

        var affected = await context.Packages
            .Where(record => record.Name == name
                && record.Version == version
                && record.State == (int)PackageState.BuildRequested)
            .ExecuteUpdateAsync(
                setters => setters
                    .SetProperty(record => record.SignatureStatus, signatureStatus)
                    .SetProperty(record => record.SignatureAlgorithm, signatureAlgorithm)
                    .SetProperty(record => record.SignatureValue, signatureValue),
                cancellationToken)
            .ConfigureAwait(false);

        if (affected == 0)
        {
            throw new PackageConflictException(
                package.Key.ToString(),
                $"Package {package.Key} signature could not be persisted because the persisted state changed.");
        }
    }

    public async Task MarkPublishedAsync(
        Package package,
        DateTimeOffset publishedAt,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(package);

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);
        var name = package.Key.Name.Value;
        var version = package.Key.Version.ToString();
        var contentHash = package.Manifest.ContentHash.Value;
        var signatureStatus = (int)PackageSignatureStatus.Signed;
        var signatureAlgorithm = package.Manifest.Signature.Algorithm;
        var signatureValue = package.Manifest.Signature.Value;
        var state = (int)PackageState.Published;

        var affected = await context.Packages
            .Where(record => record.Name == name
                && record.Version == version
                && record.State == (int)PackageState.BuildRequested
                && record.SignatureStatus == signatureStatus
                && record.ContentHash == contentHash
                && record.SignatureAlgorithm == signatureAlgorithm
                && record.SignatureValue == signatureValue)
            .ExecuteUpdateAsync(
                setters => setters
                    .SetProperty(record => record.State, state)
                    .SetProperty(record => record.PublishedAt, publishedAt),
                cancellationToken)
            .ConfigureAwait(false);

        if (affected == 1)
        {
            return;
        }

        var current = await context.Packages
            .AsNoTracking()
            .SingleOrDefaultAsync(
                record => record.Name == name && record.Version == version,
                cancellationToken)
            .ConfigureAwait(false);

        if (current is { State: (int)PackageState.Published, PublishedAt: not null }
            && current.SignatureStatus == signatureStatus
            && string.Equals(current.ContentHash, contentHash, StringComparison.Ordinal)
            && string.Equals(current.SignatureAlgorithm, signatureAlgorithm, StringComparison.Ordinal)
            && string.Equals(current.SignatureValue, signatureValue, StringComparison.Ordinal))
        {
            return;
        }

        throw new PackageConflictException(
            package.Key.ToString(),
            $"Package {package.Key} could not be marked published because its persisted state, hash, or signature changed.");
    }

    public async Task AcknowledgeDomainEventsAsync(
        PackageKey key,
        IReadOnlyCollection<IDomainEvent> publishedEvents,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(publishedEvents);
        cancellationToken.ThrowIfCancellationRequested();

        var name = key.Name.Value;
        var version = key.Version.ToString();

        await PostgresPendingEventCas.ApplyAsync(
            key,
            async token =>
            {
                await using var context = await _contextFactory
                    .CreateDbContextAsync(token)
                    .ConfigureAwait(false);
                var record = await context.Packages
                    .AsNoTracking()
                    .SingleOrDefaultAsync(
                        candidate => candidate.Name == name
                            && candidate.Version == version,
                        token)
                    .ConfigureAwait(false);

                return record?.PendingEventsJson;
            },
            async (original, updated, token) =>
            {
                await using var context = await _contextFactory
                    .CreateDbContextAsync(token)
                    .ConfigureAwait(false);
                var affected = await context.Packages
                    .Where(record => record.Name == name
                        && record.Version == version
                        && record.PendingEventsJson == original)
                    .ExecuteUpdateAsync(
                        setters => setters.SetProperty(
                            record => record.PendingEventsJson,
                            updated),
                        token)
                    .ConfigureAwait(false);

                return affected == 1;
            },
            publishedEvents,
            cancellationToken).ConfigureAwait(false);
    }

    public static IQueryable<PackageRecord> BuildSearchQuery(
        IQueryable<PackageRecord> source,
        string? search)
    {
        ArgumentNullException.ThrowIfNull(source);

        var term = search?.Trim();

        if (!string.IsNullOrWhiteSpace(term))
        {
            var contains = PostgresLikePattern.Contains(term);
            var prefix = PostgresLikePattern.Prefix(term);

            source = source.Where(record =>
                EF.Functions.ILike(record.Name, contains, "\\")
                || EF.Functions.ILike(record.SourceCommit, prefix, "\\")
                || EF.Functions.ILike(record.Provenance, contains, "\\"));
        }

        return source
            .OrderBy(record => EF.Functions.Collate(record.Name, "C"))
            .ThenBy(record => record.VersionMajor)
            .ThenBy(record => record.VersionMinor)
            .ThenBy(record => record.VersionPatch)
            .ThenBy(record => record.VersionPreRelease == null)
            .ThenBy(record => EF.Functions.Collate(record.VersionPreRelease!, "C"))
            .ThenBy(record => record.VersionBuildMetadata == null ? 0 : 1)
            .ThenBy(record => EF.Functions.Collate(record.VersionBuildMetadata!, "C"))
            .ThenBy(record => EF.Functions.Collate(record.Version, "C"))
            .Take(MaxListResults);
    }

    public async Task<IReadOnlyList<PackageIndexEntry>> SearchAsync(
        string? search,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        await using var context = await _contextFactory
            .CreateDbContextAsync(cancellationToken)
            .ConfigureAwait(false);

        var records = await BuildSearchQuery(context.Packages.AsNoTracking(), search)
            .ToListAsync(cancellationToken)
            .ConfigureAwait(false);

        return records
            .Select(record => new PackageIndexEntry(
                PostgresPackageMapper.ToManifest(record),
                PostgresPackageMapper.ToState(record.State),
                record.RegisteredAt))
            .ToArray();
    }

    private static bool IsUniqueViolation(DbUpdateException exception) =>
        exception.InnerException is PostgresException
        {
            SqlState: PostgresErrorCodes.UniqueViolation
        };
}
