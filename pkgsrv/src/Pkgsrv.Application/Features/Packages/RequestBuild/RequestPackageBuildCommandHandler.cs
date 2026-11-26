using Arlirad.Ervi.Mediator.Interfaces;
using Pkgsrv.Application.Interfaces.Builds;
using Pkgsrv.Application.Interfaces.Caching;
using Pkgsrv.Application.Interfaces.Events;
using Pkgsrv.Application.Interfaces.Persistence;
using Pkgsrv.Application.Interfaces.Security;
using Pkgsrv.Application.Interfaces.Time;
using Pkgsrv.Application.Contracts;
using Pkgsrv.Application.Errors;
using Pkgsrv.Application.Validation;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Application.Features.Packages.RequestBuild;

public sealed class RequestPackageBuildCommandHandler
    : IRequestHandler<RequestPackageBuildCommand, RequestBuildResult>
{
    private readonly IPackageRepository _repository;
    private readonly IBuildSandbox _buildSandbox;
    private readonly IIrvaAuthorization _authorization;
    private readonly IDomainEventPublisher _eventPublisher;
    private readonly ISystemClock _clock;
    private readonly IPackageCacheInvalidationNotifier? _cacheNotifier;

    public RequestPackageBuildCommandHandler(
        IPackageRepository repository,
        IBuildSandbox buildSandbox,
        IIrvaAuthorization authorization,
        IDomainEventPublisher eventPublisher,
        ISystemClock clock,
        IPackageCacheInvalidationNotifier? cacheNotifier = null)
    {
        _repository = repository;
        _buildSandbox = buildSandbox;
        _authorization = authorization;
        _eventPublisher = eventPublisher;
        _clock = clock;
        _cacheNotifier = cacheNotifier;
    }

    public async ValueTask<RequestBuildResult> Handle(
        RequestPackageBuildCommand request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        var failures = RequestBuildValidator.Validate(request);

        if (failures.Count > 0)
        {
            throw new ValidationFailedException(failures);
        }

        var key = PackageKey.Create(request.Name, request.Version);
        var actor = request.Actor ?? AuthorizationContext.Anonymous;
        var authorization = await _authorization.AuthorizeAsync(
            actor,
            PackageAction.Build,
            key,
            cancellationToken).ConfigureAwait(false);
        authorization.EnsureAuthorized(nameof(PackageAction.Build));

        var package = await _repository.FindAsync(key, cancellationToken).ConfigureAwait(false);

        if (package is null)
        {
            throw new PackageNotFoundException(key.ToString());
        }

        package.RequestBuild(_clock.UtcNow);
        await _repository.UpdateAsync(package, cancellationToken).ConfigureAwait(false);

        if (_cacheNotifier is not null)
        {
            await _cacheNotifier
                .NotifyAsync(
                    PackageCacheInvalidation.ForPackage(package.Key),
                    CancellationToken.None)
                .ConfigureAwait(false);
        }

        var domainEvents = package.PeekDomainEvents();
        await _eventPublisher.PublishAsync(
            domainEvents,
            cancellationToken).ConfigureAwait(false);
        await _repository.AcknowledgeDomainEventsAsync(
            package.Key,
            domainEvents,
            cancellationToken).ConfigureAwait(false);
        package.AcknowledgeDomainEvents(domainEvents);

        var result = await _buildSandbox.ExecuteAsync(
            new BuildSandboxRequest(
                package.Key,
                package.Manifest.Source,
                package.BuildPolicy,
                package.Manifest.Toolchain,
                package.Manifest.ContentHash),
            cancellationToken).ConfigureAwait(false);

        return new RequestBuildResult(
            key.Name.Value,
            key.Version.ToString(),
            result.Status.ToString(),
            result.Detail,
            result.Status != BuildExecutionStatus.NotExecuted);
    }
}
