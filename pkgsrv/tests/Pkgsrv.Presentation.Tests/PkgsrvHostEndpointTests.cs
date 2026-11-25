using System.Net;
using System.Net.Sockets;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Pkgsrv.Presentation.Configuration;

namespace Pkgsrv.Presentation.Tests;

public sealed class PkgsrvHostEndpointTests
{
    [Fact]
    public async Task UnixSocketHost_ServesHealthOverConfiguredSocket()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"pkgsrv-socket-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var socketPath = Path.Combine(directory, "pkgsrv.sock");

        try
        {
            await using var app = PkgsrvHost.Create(
                [$"--Pkgsrv:Listen:UnixSocketPath={socketPath}"]);

            await app.StartAsync();

            Assert.Contains(socketPath, Directory.EnumerateFileSystemEntries(directory));
            Assert.All(
                app.Urls,
                url => Assert.StartsWith("http://unix:", url, StringComparison.OrdinalIgnoreCase));

            using var client = CreateUnixSocketClient(socketPath);
            var response = await client.GetAsync("/healthz");

            Assert.Equal(HttpStatusCode.OK, response.StatusCode);
            Assert.Contains("ok", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);

            await app.StopAsync();
        }
        finally
        {
            if (Directory.Exists(directory))
            {
                Directory.Delete(directory, recursive: true);
            }
        }
    }

    [Fact]
    public async Task TcpOverride_HostListensOnLoopbackOnlyWhenExplicitlyEnabled()
    {
        await using var app = PkgsrvHost.Create(
        [
            "--Pkgsrv:Listen:Tcp:Enabled=true",
            "--Pkgsrv:Listen:Tcp:Url=http://127.0.0.1:0"
        ]);

        await app.StartAsync();

        var addresses = app.Services
            .GetRequiredService<IServer>()
            .Features
            .Get<IServerAddressesFeature>()!
            .Addresses;
        var address = Assert.Single(addresses);

        using var client = new HttpClient();
        var response = await client.GetAsync($"{address.TrimEnd('/')}/healthz");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        await app.StopAsync();
    }

    [Fact]
    public async Task Host_RegistersResolvedListenOptions()
    {
        await using var app = PkgsrvHost.Create([]);

        var options = app.Services.GetRequiredService<PkgsrvListenOptions>();

        Assert.Equal(PkgsrvListenOptions.DefaultUnixSocketPath, options.UnixSocketPath);
        Assert.False(options.TcpEnabled);
    }

    private static HttpClient CreateUnixSocketClient(string socketPath)
    {
        var handler = new SocketsHttpHandler
        {
            ConnectCallback = async (_, cancellationToken) =>
            {
                var socket = new Socket(
                    AddressFamily.Unix,
                    SocketType.Stream,
                    ProtocolType.Unspecified);

                try
                {
                    await socket.ConnectAsync(
                        new UnixDomainSocketEndPoint(socketPath),
                        cancellationToken);
                    return new NetworkStream(socket, ownsSocket: true);
                }
                catch
                {
                    socket.Dispose();
                    throw;
                }
            }
        };

        return new HttpClient(handler)
        {
            BaseAddress = new Uri("http://localhost")
        };
    }
}
