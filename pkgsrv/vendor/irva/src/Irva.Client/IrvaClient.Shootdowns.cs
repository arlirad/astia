using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using Irva.Dtos;

namespace Irva.Client;

public sealed partial class IrvaClient
{
    private static readonly JsonSerializerOptions _shootdownJson = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    public async IAsyncEnumerable<ShootdownEvent> StreamShootdownsAsync([EnumeratorCancellation] CancellationToken ct = default)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, "/api/auth/verify/events");
        if (_defaultToken is not null)
            request.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", _defaultToken);

        using var response = await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct);
        response.EnsureSuccessStatusCode();

        await using var stream = await response.Content.ReadAsStreamAsync(ct);
        using var reader = new StreamReader(stream, Encoding.UTF8);

        var eventType = (string?)null;
        var data = new StringBuilder();

        while (!ct.IsCancellationRequested)
        {
            var line = await reader.ReadLineAsync(ct);
            if (line is null)
                yield break;

            if (line.Length == 0)
            {
                if (eventType is not null)
                {
                    var evt = ParseShootdown(eventType, data.ToString());
                    if (evt is not null)
                        yield return evt;
                }

                eventType = null;
                data.Clear();
                continue;
            }

            if (line.StartsWith("event:", StringComparison.OrdinalIgnoreCase))
                eventType = line["event:".Length..].Trim();
            else if (line.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
                data.AppendLine(line["data:".Length..].Trim());
        }
    }

    private static ShootdownEvent? ParseShootdown(string eventType, string data)
    {
        return eventType switch
        {
            "account_invalidated" => ParseTargeted(data),
            "all_invalidated" => new ShootdownEvent.Global(),
            _ => null
        };
    }

    private static ShootdownEvent.Targeted? ParseTargeted(string data)
    {
        try
        {
            var doc = JsonDocument.Parse(data);
            return doc.RootElement.TryGetProperty("accountId", out var idEl) && Guid.TryParse(idEl.GetString(), out var accountId)
                ? new ShootdownEvent.Targeted(accountId)
                : null;
        }
        catch
        {
            return null;
        }
    }
}
