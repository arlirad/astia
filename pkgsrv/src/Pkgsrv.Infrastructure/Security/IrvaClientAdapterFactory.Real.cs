using System.Net.Http;
using Irva.Client;

namespace Pkgsrv.Infrastructure.Security;

public static class IrvaClientAdapterFactory
{
    public static bool IsAvailable => true;

    public static IIrvaClientAdapter Create(IrvaClientConnectionSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);

        return new RealIrvaClientAdapter(new IrvaClient(new IrvaClientOptions
        {
            BaseUrl = settings.BaseUrl,
            AppToken = settings.AppToken,
            SocketAddress = settings.SocketAddress
        }));
    }

    private sealed class RealIrvaClientAdapter : IIrvaClientAdapter, IDisposable
    {
        private readonly IrvaClient _client;

        public RealIrvaClientAdapter(IrvaClient client)
        {
            _client = client;
        }

        public async Task<IrvaVerifiedAccount> VerifyAsync(
            string accessToken,
            CancellationToken cancellationToken)
        {
            try
            {
                var response = await _client
                    .VerifyAsync(accessToken, cancellationToken)
                    .ConfigureAwait(false);

                return new IrvaVerifiedAccount(response.AccountId, response.Username);
            }
            catch (IrvaApiException exception) when (exception.StatusCode is >= 400 and < 500)
            {
                throw new IrvaClientAuthenticationException(
                    "The bearer access token was rejected by Irva.");
            }
            catch (IrvaApiException)
            {
                throw new IrvaClientUnavailableException(
                    "The Irva authorization endpoint returned an error.");
            }
            catch (HttpRequestException)
            {
                throw new IrvaClientUnavailableException(
                    "The Irva authorization endpoint is unreachable.");
            }
            catch (TaskCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new IrvaClientUnavailableException(
                    "The Irva authorization request timed out.");
            }
            catch (System.Text.Json.JsonException)
            {
                throw new IrvaClientUnavailableException(
                    "The Irva authorization response was invalid.");
            }
        }

        public bool HasPermission(Guid accountId, string permission) =>
            _client.HasPermission(accountId, permission);

        public void Dispose() => _client.Dispose();
    }
}
