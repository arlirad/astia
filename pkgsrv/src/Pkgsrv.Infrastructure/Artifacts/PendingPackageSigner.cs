using Pkgsrv.Application.Abstractions.Artifacts;
using Pkgsrv.Domain.Packages;

namespace Pkgsrv.Infrastructure.Artifacts;

public sealed class PendingPackageSigner : IPackageSigner
{
    public Task<SigningResult> SignAsync(ContentHash contentHash, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(contentHash);

        return Task.FromResult(new SigningResult(
            PackageSignature.Pending,
            "Artifact signing integration is pending; the artifact remains unsigned."));
    }
}
