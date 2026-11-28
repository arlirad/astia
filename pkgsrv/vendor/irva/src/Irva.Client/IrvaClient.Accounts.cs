using Irva.Dtos;

namespace Irva.Client;

public sealed partial class IrvaClient
{
    public Task<CreateResponse> CreateAccountAsync(string username, string password, CancellationToken ct = default)
        => PostAsync<CreateResponse>("/api/accounts", new {
            username,
            password,
        }, ct);

    public Task<ListAccountsResponse> ListAccountsAsync(CancellationToken ct = default)
        => GetAsync<ListAccountsResponse>("/api/accounts", ct);

    public Task<ListAccountsResponse> ListAccountsAsync(int offset, int limit, CancellationToken ct = default)
        => GetAsync<ListAccountsResponse>($"/api/accounts?offset={offset}&limit={limit}", ct);

    public Task<AccountSummary> GetAccountByUsernameAsync(string username, CancellationToken ct = default)
        => GetAsync<AccountSummary>($"/api/accounts/by-username/{Uri.EscapeDataString(username)}", ct);

    public Task GrantRoleAsync(Guid accountId, Guid roleId, CancellationToken ct = default)
        => PostAsync($"/api/accounts/{accountId}/roles/{roleId}/grant", ct);

    public Task RevokeRoleAsync(Guid accountId, Guid roleId, CancellationToken ct = default)
        => PostAsync($"/api/accounts/{accountId}/roles/{roleId}/revoke", ct);

    public Task GrantTreeOverseeAsync(Guid accountId, Guid permissionTreeId, CancellationToken ct = default)
        => PostAsync($"/api/accounts/{accountId}/trees/{permissionTreeId}/oversee/grant", ct);

    public Task RevokeTreeOverseeAsync(Guid accountId, Guid permissionTreeId, CancellationToken ct = default)
        => PostAsync($"/api/accounts/{accountId}/trees/{permissionTreeId}/oversee/revoke", ct);

    public Task<AccountSummary> GetAccountByIdAsync(Guid accountId, CancellationToken ct = default)
        => GetAsync<AccountSummary>($"/api/accounts/{accountId}", ct);

    public Task ChangeAccountUsernameAsync(Guid accountId, string username, CancellationToken ct = default)
        => PutAsync($"/api/accounts/{accountId}/username", new {
            username,
        }, ct);

    public Task DeleteAccountAsync(Guid accountId, CancellationToken ct = default)
        => DeleteAsync($"/api/accounts/{accountId}", ct);
}