using Pkgsrv.Application.Interfaces.Time;

namespace Pkgsrv.Infrastructure.Time;

public sealed class SystemClock : ISystemClock
{
    public DateTimeOffset UtcNow => DateTimeOffset.UtcNow;
}
