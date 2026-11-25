namespace Pkgsrv.Domain.Events;

public interface IDomainEvent
{
    DateTimeOffset OccurredAt { get; }
}
