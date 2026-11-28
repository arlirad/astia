using Irva.Dtos;

namespace Irva.Client;

public sealed partial class IrvaClient
{
    public Task<AttestResponse> AttestAsync(string username, string password, CancellationToken ct = default)
        => PostAsync<AttestResponse>("/api/auth/attest", new AttestRequest(username, password), ct);

    public Task<AttestResponse> RefreshAsync(string refreshToken, CancellationToken ct = default)
        => PostAsync<AttestResponse>("/api/auth/refresh", new RefreshRequest(refreshToken), ct);

    public Task<WhoAmIResponse> WhoAmIAsync(string accessToken, CancellationToken ct = default)
    {
        var req = AuthedRequest(HttpMethod.Get, "/api/auth/whoami");
        req.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", accessToken);
        return SendAsync<WhoAmIResponse>(req, ct);
    }

    public Task RevokeTokenAsync(string refreshToken, CancellationToken ct = default)
        => PostAsync("/api/auth/revoke", new RevokeRequest(refreshToken), ct);

    public Task ChangePasswordAsync(string accessToken, string oldPassword, string newPassword, CancellationToken ct = default)
    {
        var req = AuthedRequest(HttpMethod.Post, "/api/auth/change-password", new {
            oldPassword,
            newPassword,
        });
        req.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", accessToken);
        return SendVoidAsync(req, ct);
    }

    public Task<MyPermissionsResponse> MyPermissionsAsync(string accessToken, CancellationToken ct = default)
    {
        var req = AuthedRequest(HttpMethod.Get, "/api/auth/permissions");
        req.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", accessToken);
        return SendAsync<MyPermissionsResponse>(req, ct);
    }

    public async Task<VerifyTokenResponse> VerifyAsync(string userAccessToken, CancellationToken ct = default)
    {
        var result = await PostAsync<VerifyTokenResponse>("/api/auth/verify", new VerifyTokenRequest(userAccessToken), ct);
        CachePermissions(result);
        return result;
    }
}