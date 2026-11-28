using System.Net.Http.Json;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Irva.Dtos;

namespace Irva.Client;

/// <summary>
/// Thrown when the Irva API returns a non-success HTTP status code.
/// </summary>
public class IrvaApiException(int statusCode, string errorBody) : Exception($"API request failed with status {statusCode}: {errorBody}")
{
    /// <summary>
    /// The HTTP status code returned by the server.
    /// </summary>
    public int StatusCode
    {
        get;
    } = statusCode;

    /// <summary>
    /// The raw response body from the server.
    /// </summary>
    public string ErrorBody
    {
        get;
    } = errorBody;
}

/// <summary>
/// Typed async HTTP client for the Irva IAM API.
/// </summary>
/// <remarks>
/// <para>
/// Construct with <see cref="IrvaClientOptions"/> to configure the base URL and
/// default authentication (either <see cref="IrvaClientOptions.MasterKey"/> or
/// <see cref="IrvaClientOptions.AppToken"/>).
/// </para>
/// <para>
/// All methods throw <see cref="IrvaApiException"/> on non-success responses.
/// The client is <see cref="IDisposable"/> and owns its internal
/// <see cref="HttpClient"/> when created from options.
/// </para>
/// </remarks>
public sealed partial class IrvaClient : IDisposable
{
    private readonly HttpClient _http;
    private readonly bool _ownsHttp;
    private readonly JsonSerializerOptions _json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, PropertyNameCaseInsensitive = true };
    private readonly string? _defaultToken;

    /// <summary>
    /// Creates a new <see cref="IrvaClient"/> from the given <paramref name="options"/>.
    /// </summary>
    /// <param name="options">Client configuration including base URL and authentication.</param>
    /// <exception cref="ArgumentException">
    /// Thrown if <see cref="IrvaClientOptions.BaseUrl"/> is not a valid absolute URI,
    /// or uses a non-HTTPS scheme for non-localhost addresses.
    /// </exception>
    public IrvaClient(IrvaClientOptions options)
    {
        var baseUrl = options.BaseUrl.TrimEnd('/');
        if (!Uri.TryCreate(baseUrl, UriKind.Absolute, out var uri))
            throw new ArgumentException("Invalid BaseUrl: not a valid URI.", nameof(options.BaseUrl));

        if (uri.Scheme != "https" && uri.Host != "localhost" && uri.Host != "127.0.0.1" && string.IsNullOrEmpty(options.SocketAddress))
            throw new ArgumentException("BaseUrl must use HTTPS for non-localhost addresses.", nameof(options.BaseUrl));

        if (!string.IsNullOrEmpty(options.SocketAddress))
        {
            var socketPath = options.SocketAddress;
            var handler = new SocketsHttpHandler
            {
                ConnectCallback = async (_, ct) =>
                {
                    var socket = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
                    await socket.ConnectAsync(new UnixDomainSocketEndPoint(socketPath), ct);
                    return new NetworkStream(socket, ownsSocket: true);
                }
            };
            _http = new HttpClient(handler) { BaseAddress = uri };
        }
        else
        {
            _http = new HttpClient { BaseAddress = uri };
        }

        _ownsHttp = true;
        _defaultToken = options.MasterKey ?? options.AppToken;

        if (_defaultToken is not null)
            _http.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", _defaultToken);
    }

    /// <summary>
    /// Creates a new <see cref="IrvaClient"/> wrapping an existing <see cref="HttpClient"/>.
    /// </summary>
    /// <param name="http">The HTTP client to use for all requests.</param>
    /// <param name="ownsHttp">
    /// If <see langword="true"/>, disposing this <see cref="IrvaClient"/> will also dispose
    /// the provided <paramref name="http"/> client.
    /// </param>
    internal IrvaClient(HttpClient http, bool ownsHttp = false)
    {
        _http = http;
        _ownsHttp = ownsHttp;
    }

    /// <summary>
    /// Disposes the internal <see cref="HttpClient"/> if this instance owns it.
    /// </summary>
    public void Dispose()
    {
        StopShootdownListener();
        if (_ownsHttp)
            _http.Dispose();
    }

    private async Task<T> GetAsync<T>(string path, CancellationToken ct)
    {
        var resp = await _http.GetAsync(path, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
        var result = await resp.Content.ReadFromJsonAsync<T>(_json, ct);
        return result ?? throw new System.Text.Json.JsonException("Response body was null or could not be deserialized.");
    }

    private async Task<T> PostAsync<T>(string path, object? body, CancellationToken ct)
    {
        var resp = body is null
            ? await _http.PostAsync(path, null, ct)
            : await _http.PostAsJsonAsync(path, body, _json, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
        var result = await resp.Content.ReadFromJsonAsync<T>(_json, ct);
        return result ?? throw new System.Text.Json.JsonException("Response body was null or could not be deserialized.");
    }

    private async Task PostAsync(string path, CancellationToken ct)
    {
        var resp = await _http.PostAsync(path, null, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
    }

    private async Task PostAsync(string path, object body, CancellationToken ct)
    {
        var resp = await _http.PostAsJsonAsync(path, body, _json, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
    }

    private async Task PutAsync(string path, object? body, CancellationToken ct)
    {
        var resp = body is null
            ? await _http.PutAsync(path, null, ct)
            : await _http.PutAsJsonAsync(path, body, _json, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
    }

    private async Task DeleteAsync(string path, CancellationToken ct)
    {
        var resp = await _http.DeleteAsync(path, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
    }

    private async Task<T> SendAsync<T>(HttpRequestMessage request, CancellationToken ct)
    {
        var resp = await _http.SendAsync(request, ct);

        if ((int)resp.StatusCode >= 300)
        {
            var errorBody = await resp.Content.ReadAsStringAsync(ct);
            throw new IrvaApiException((int)resp.StatusCode, errorBody);
        }

        var result = await resp.Content.ReadFromJsonAsync<T>(_json, ct);
        return result ?? throw new System.Text.Json.JsonException("Response body was null or could not be deserialized.");
    }

    private async Task SendVoidAsync(HttpRequestMessage request, CancellationToken ct)
    {
        var resp = await _http.SendAsync(request, ct);
        if (!resp.IsSuccessStatusCode)
            throw new IrvaApiException((int)resp.StatusCode, await resp.Content.ReadAsStringAsync(ct));
    }

    private HttpRequestMessage Request(HttpMethod method, string path, object? body = null)
    {
        var req = new HttpRequestMessage(method, path);
        if (body is not null)
            req.Content = new StringContent(JsonSerializer.Serialize(body, _json), encoding: Encoding.UTF8, mediaType: "application/json");
        return req;
    }

    private HttpRequestMessage AuthedRequest(HttpMethod method, string path, object? body = null)
    {
        var req = Request(method, path, body);
        if (_defaultToken is not null)
            req.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", _defaultToken);
        return req;
    }
}
