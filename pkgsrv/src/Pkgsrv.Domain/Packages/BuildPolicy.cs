using Pkgsrv.Domain.Errors;

namespace Pkgsrv.Domain.Packages;

public enum BuildIsolation
{
    Namespace = 0,
    Container = 1,
    VirtualMachine = 2
}

public sealed record BuildPolicy
{
    public static readonly TimeSpan MinimumTimeout = TimeSpan.FromSeconds(1);
    public static readonly TimeSpan MaximumTimeout = TimeSpan.FromHours(1);

    private BuildPolicy(BuildIsolation isolation, bool allowNetwork, TimeSpan timeout)
    {
        Isolation = isolation;
        AllowNetwork = allowNetwork;
        Timeout = timeout;
    }

    public BuildIsolation Isolation { get; }

    public bool AllowNetwork { get; }

    public TimeSpan Timeout { get; }

    public static BuildPolicy Create(BuildIsolation isolation, bool allowNetwork, TimeSpan timeout)
    {
        if (!Enum.IsDefined(isolation))
        {
            throw new DomainException(DomainErrorCodes.BuildPolicyInvalid, "Build isolation mode is invalid.");
        }

        if (timeout < MinimumTimeout || timeout > MaximumTimeout)
        {
            throw new DomainException(
                DomainErrorCodes.BuildPolicyInvalid,
                "Build timeout must be between 1 second and 1 hour.");
        }

        return new BuildPolicy(isolation, allowNetwork, timeout);
    }
}
