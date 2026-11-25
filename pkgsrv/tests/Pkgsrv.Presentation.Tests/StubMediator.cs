using Arlirad.Ervi.Mediator.Interfaces;

namespace Pkgsrv.Presentation.Tests;

internal sealed class StubMediator : IMediator
{
    public Func<object, CancellationToken, ValueTask<object>>? Handler { get; set; }

    public object? LastRequest { get; private set; }

    public int Calls { get; private set; }

    public ValueTask<TResponse> Send<TResponse>(
        IRequest<TResponse> request,
        CancellationToken ct)
    {
        LastRequest = request;
        Calls++;
        var handler = Handler ?? throw new InvalidOperationException("No stub handler is configured.");
        return AwaitResponse<TResponse>(handler, request, ct);
    }

    public ValueTask Publish<TNotification>(TNotification notification, CancellationToken ct)
        where TNotification : INotification =>
        ValueTask.CompletedTask;

    private static async ValueTask<TResponse> AwaitResponse<TResponse>(
        Func<object, CancellationToken, ValueTask<object>> handler,
        object request,
        CancellationToken ct)
    {
        var response = await handler(request, ct);
        return (TResponse)response;
    }
}
