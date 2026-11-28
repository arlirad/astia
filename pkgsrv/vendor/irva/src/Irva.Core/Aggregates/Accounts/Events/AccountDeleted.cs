using Irva.Core.Interfaces;

namespace Irva.Core.Aggregates.Accounts.Events;

public sealed class AccountDeleted(Guid id) : IEvent
{
    public Guid Id => id;
}