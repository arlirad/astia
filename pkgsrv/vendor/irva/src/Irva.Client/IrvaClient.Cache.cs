using System.Collections.Concurrent;
using Irva.Core.Aggregates.PermissionTrees.Entities;
using Irva.Dtos;

namespace Irva.Client;

public sealed partial class IrvaClient
{
    private readonly ConcurrentDictionary<Guid, Dictionary<string, ClaimStatus>> _permissionCache = new();
    private CancellationTokenSource? _shootdownCts;

    public void StartShootdownListener()
    {
        _shootdownCts = new CancellationTokenSource();
        _ = ListenLoopAsync(_shootdownCts.Token);
    }

    public void StopShootdownListener()
    {
        _shootdownCts?.Cancel();
        _shootdownCts?.Dispose();
        _shootdownCts = null;
    }

    private void CachePermissions(VerifyTokenResponse response)
    {
        var permissions = new Dictionary<string, ClaimStatus>(StringComparer.OrdinalIgnoreCase);
        foreach (var app in response.Applications)
            foreach (var perm in app.Permissions)
                if (perm.FullName is not null)
                    permissions[perm.FullName] = perm.Status;

        _permissionCache[response.AccountId] = permissions;
    }

    private async Task ListenLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await foreach (var evt in StreamShootdownsAsync(ct))
                {
                    switch (evt)
                    {
                        case ShootdownEvent.Targeted t:
                            _permissionCache.TryRemove(t.AccountId, out _);
                            break;
                        case ShootdownEvent.Global:
                            _permissionCache.Clear();
                            break;
                    }
                }
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                return;
            }
            catch
            {
                await Task.Delay(TimeSpan.FromSeconds(5), ct);
            }
        }
    }
}
