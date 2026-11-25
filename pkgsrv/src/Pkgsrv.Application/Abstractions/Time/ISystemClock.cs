namespace Pkgsrv.Application.Abstractions.Time;

public interface ISystemClock
{
    DateTimeOffset UtcNow { get; }
}
