using Pkgsrv.Infrastructure.Security;

namespace Pkgsrv.Presentation.Tests;

[AttributeUsage(AttributeTargets.Method)]
public sealed class DefaultIrvaBuildFactAttribute : FactAttribute
{
    public DefaultIrvaBuildFactAttribute()
    {
        if (IrvaClientAdapterFactory.IsAvailable)
        {
            Skip = "Requires the default build without IrvaClientProject.";
        }
    }
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class RealIrvaBuildFactAttribute : FactAttribute
{
    public RealIrvaBuildFactAttribute()
    {
        if (!IrvaClientAdapterFactory.IsAvailable)
        {
            Skip = "Requires -p:IrvaClientProject=<path-to-Irva.Client.csproj>.";
        }
    }
}
