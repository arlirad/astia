using Pkgsrv.Application.Abstractions.Time;

namespace Pkgsrv.Infrastructure.Time;

public sealed class SystemClock : ISystemClock
{
    public DateTimeOffset UtcNow => DateTimeOffset.UtcNow;
}
