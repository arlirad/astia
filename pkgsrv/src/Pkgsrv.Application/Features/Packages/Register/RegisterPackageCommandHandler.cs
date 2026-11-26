using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Sources;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Features.Packages.Register;

public sealed class RegisterPackageCommandHandler
    : IRequestHandler<RegisterPackageCommand, RegisterPackageResult>
{
    private readonly IPackageRepository _repository;
    private readonly ISourceAllowlist _sourceAllowlist;
    private readonly IIrvaAuthorization _authorization;
    private readonly IDomainEventPublisher _eventPublisher;
    private readonly ISystemClock _clock;
    private readonly IPackageCacheInvalidator? _cacheInvalidator;

    public RegisterPackageCommandHandler(
        IPackageRepository repository,
        ISourceAllowlist sourceAllowlist,
        IIrvaAuthorization authorization,
        IDomainEventPublisher eventPublisher,
        ISystemClock clock,
        IPackageCacheInvalidator? cacheInvalidator = null)
    {
        _repository = repository;
        _sourceAllowlist = sourceAllowlist;
        _authorization = authorization;
        _eventPublisher = eventPublisher;
        _clock = clock;
        _cacheInvalidator = cacheInvalidator;
    }

    public async ValueTask<RegisterPackageResult> Handle(
        RegisterPackageCommand request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        var failures = PackageRegistrationValidator.Validate(request);

        if (failures.Count > 0)
        {
            throw new ValidationFailedException(failures);
        }

        var actor = request.Actor ?? AuthorizationContext.Anonymous;
        var authorization = await _authorization.AuthorizeAsync(
            actor,
            PackageAction.Publish,
            null,
            cancellationToken).ConfigureAwait(false);
        authorization.EnsureAuthorized(nameof(PackageAction.Publish));

        var source = PackageSource.Create(request.SourceUrl, request.SourceCommit);

        if (!_sourceAllowlist.IsAllowed(source))
        {
            throw new SourceNotAllowedException(source.Host);
        }

        var manifest = PackageManifest.Create(
            PackageName.Create(request.Name),
            PackageVersion.Create(request.Version),
            source,
            Toolchain.Create(request.ToolchainName, request.ToolchainVersion),
            ContentHash.Create(request.ContentHash),
            PackageProvenance.Create(request.Provenance),
            string.IsNullOrWhiteSpace(request.Signature)
                ? PackageSignature.Pending
                : PackageSignature.Signed(request.SignatureAlgorithm, request.Signature));

        var buildPolicy = BuildPolicy.Create(
            Enum.Parse<BuildIsolation>(request.Isolation!, true),
            request.AllowNetwork,
            TimeSpan.FromSeconds(request.TimeoutSeconds));

        var package = Package.Register(manifest, buildPolicy, _clock.UtcNow);

        if (!await _repository.TryAddAsync(package, cancellationToken).ConfigureAwait(false))
        {
            throw new PackageConflictException(package.Key.ToString());
        }

        _cacheInvalidator?.InvalidateAfterMutation();

        var domainEvents = package.PeekDomainEvents();
        await _eventPublisher.PublishAsync(
            domainEvents,
            cancellationToken).ConfigureAwait(false);
        await _repository.AcknowledgeDomainEventsAsync(
            package.Key,
            domainEvents,
            cancellationToken).ConfigureAwait(false);
        package.AcknowledgeDomainEvents(domainEvents);

        return new RegisterPackageResult(PackageDetailsDto.From(package));
    }
}
