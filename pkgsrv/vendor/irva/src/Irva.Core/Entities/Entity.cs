using Irva.Core.Interfaces;

namespace Irva.Core.Entities;

public abstract class Entity
{
    public Guid Id
    {
        get; init;
    }
    public DateTime CreatedAt { get; init; } = DateTime.UtcNow;
    public DateTime UpdatedAt { get; private set; } = DateTime.UtcNow;

    internal Entity()
    {
    }

    private List<IEvent>? _events;

    protected void Publish(IEvent @event)
    {
        _events ??= [];
        _events.Add(@event);
    }

    public IEnumerable<IEvent>? GetEvents() => _events;

    public IReadOnlyList<IEvent> PopEvents()
    {
        if (_events is null or { Count: 0 })
            return [];

        var events = new List<IEvent>(_events);
        _events.Clear();
        return events.AsReadOnly();
    }
}