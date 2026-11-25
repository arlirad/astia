using Pkgsrv.Application.Abstractions.Security;

namespace Pkgsrv.Application.Authorization;

public static class PackagePermissions
{
    public const string Read = "read";
    public const string Build = "build";
    public const string Publish = "publish";

    public static string For(PackageAction action) => action switch
    {
        PackageAction.Read => Read,
        PackageAction.Build => Build,
        PackageAction.Publish => Publish,
        _ => throw new ArgumentOutOfRangeException(nameof(action), action, null)
    };
}
