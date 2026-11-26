using System.Net;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Pkgsrv.Application.Interfaces.Persistence;

namespace Pkgsrv.Presentation.Tests;

public sealed class StorageReadinessEndpointTests
{
    private const string PostgresConnectionString =
        "Host=secret-db.internal;Database=pkgsrv;Username=pkgsrv;Password=supersecret";

    [Fact]
    public async Task HealthAndReady_ReportInMemoryReadyAndNotDurable()
    {
        await using var app = PkgsrvHost.Create(CreateTcpArguments());
        await app.StartAsync();
        using var client = new HttpClient();
        var address = GetAddress(app);

        var health = await client.GetAsync($"{address}/healthz");
        var ready = await client.GetAsync($"{address}/readyz");

        Assert.Equal(HttpStatusCode.OK, health.StatusCode);
        Assert.Equal("application/json", health.Content.Headers.ContentType?.MediaType);
        Assert.Contains("ok", await health.Content.ReadAsStringAsync(), StringComparison.Ordinal);

        Assert.Equal(HttpStatusCode.OK, ready.StatusCode);
        Assert.Equal("application/json", ready.Content.Headers.ContentType?.MediaType);

        var body = await ready.Content.ReadAsStringAsync();
        using var document = JsonDocument.Parse(body);
        var root = document.RootElement;

        Assert.Equal("ready", root.GetProperty("status").GetString());
        Assert.Equal("InMemory", root.GetProperty("storageProvider").GetString());
        Assert.False(root.GetProperty("storageDurable").GetBoolean());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("schemaVersion").ValueKind);
        Assert.Equal(JsonValueKind.Null, root.GetProperty("postgresAvailable").ValueKind);
        Assert.False(root.TryGetProperty("connectionString", out _));

        await app.StopAsync();
    }

    [Fact]
    public async Task Ready_Returns503WithSafePayloadWhenPostgresSchemaIsPending()
    {
        await using var app = PkgsrvHost.Create(
            CreatePostgresArguments(),
            configureServices: services =>
            {
                services.RemoveAll<IStorageReadinessProbe>();
                services.AddSingleton<IStorageReadinessProbe>(
                    new StubReadinessProbe(new StorageReadinessReport(
                        IsReady: false,
                        Provider: StorageProviderKind.Postgres,
                        IsDurable: true,
                        SchemaVersion: 2,
                        PostgresAvailable: true)));
            });
        await app.StartAsync();
        using var client = new HttpClient();
        var address = GetAddress(app);

        var health = await client.GetAsync($"{address}/healthz");
        var ready = await client.GetAsync($"{address}/readyz");

        Assert.Equal(HttpStatusCode.OK, health.StatusCode);
        Assert.Equal(HttpStatusCode.ServiceUnavailable, ready.StatusCode);
        Assert.Equal("application/json", ready.Content.Headers.ContentType?.MediaType);

        var body = await ready.Content.ReadAsStringAsync();
        using var document = JsonDocument.Parse(body);
        var root = document.RootElement;

        Assert.Equal("not_ready", root.GetProperty("status").GetString());
        Assert.Equal("Postgres", root.GetProperty("storageProvider").GetString());
        Assert.True(root.GetProperty("storageDurable").GetBoolean());
        Assert.Equal(2, root.GetProperty("schemaVersion").GetInt32());
        Assert.True(root.GetProperty("postgresAvailable").GetBoolean());
        AssertSafePayload(body);

        await app.StopAsync();
    }

    [Fact]
    public async Task Ready_Returns503JsonWhenPostgresProbeIsMissing()
    {
        await using var app = PkgsrvHost.Create(
            CreatePostgresArguments(),
            configureServices: services =>
                services.RemoveAll<IStorageReadinessProbe>());
        await app.StartAsync();
        using var client = new HttpClient();
        var address = GetAddress(app);

        var ready = await client.GetAsync($"{address}/readyz");

        Assert.Equal(HttpStatusCode.ServiceUnavailable, ready.StatusCode);
        Assert.Equal("application/json", ready.Content.Headers.ContentType?.MediaType);

        var body = await ready.Content.ReadAsStringAsync();
        using var document = JsonDocument.Parse(body);
        var root = document.RootElement;

        Assert.Equal("not_ready", root.GetProperty("status").GetString());
        Assert.Equal("Postgres", root.GetProperty("storageProvider").GetString());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("schemaVersion").ValueKind);
        Assert.False(root.GetProperty("postgresAvailable").GetBoolean());
        AssertSafePayload(body);

        await app.StopAsync();
    }

    [Fact]
    public async Task Ready_DoesNotLeakProbeFailureDetails()
    {
        await using var app = PkgsrvHost.Create(
            CreatePostgresArguments(),
            configureServices: services =>
            {
                services.RemoveAll<IStorageReadinessProbe>();
                services.AddSingleton<IStorageReadinessProbe>(
                    new ThrowingReadinessProbe(new InvalidOperationException(
                        "Host=secret-db.internal;Password=supersecret")));
            });
        await app.StartAsync();
        using var client = new HttpClient();
        var address = GetAddress(app);

        var ready = await client.GetAsync($"{address}/readyz");

        Assert.Equal(HttpStatusCode.ServiceUnavailable, ready.StatusCode);
        Assert.Equal("application/json", ready.Content.Headers.ContentType?.MediaType);
        AssertSafePayload(await ready.Content.ReadAsStringAsync());

        await app.StopAsync();
    }

    [Fact]
    public async Task Ready_Returns503JsonWhenPostgresProbeCannotBeResolved()
    {
        await using var app = PkgsrvHost.Create(
            CreatePostgresArguments(),
            configureServices: services =>
            {
                services.RemoveAll<IStorageReadinessProbe>();
                services.AddSingleton<IStorageReadinessProbe>(_ =>
                    throw new InvalidOperationException(
                        "PostgreSQL readiness dependencies are unavailable."));
            });
        await app.StartAsync();
        using var client = new HttpClient();
        var address = GetAddress(app);

        var ready = await client.GetAsync($"{address}/readyz");

        Assert.Equal(HttpStatusCode.ServiceUnavailable, ready.StatusCode);
        Assert.Equal("application/json", ready.Content.Headers.ContentType?.MediaType);

        var body = await ready.Content.ReadAsStringAsync();
        using var document = JsonDocument.Parse(body);
        var root = document.RootElement;

        Assert.Equal("not_ready", root.GetProperty("status").GetString());
        Assert.Equal("Postgres", root.GetProperty("storageProvider").GetString());
        Assert.False(root.GetProperty("postgresAvailable").GetBoolean());
        AssertSafePayload(body);

        await app.StopAsync();
    }

    private static string[] CreateTcpArguments() =>
    [
        "--Pkgsrv:Listen:Tcp:Enabled=true",
        "--Pkgsrv:Listen:Tcp:Url=http://127.0.0.1:0"
    ];

    private static string[] CreatePostgresArguments() =>
    [
        "--Pkgsrv:Database:Provider=Postgres",
        "--Pkgsrv:Database:ConnectionString=" + PostgresConnectionString,
        "--Pkgsrv:Database:InitializeSchema=true",
        "--Pkgsrv:Listen:Tcp:Enabled=true",
        "--Pkgsrv:Listen:Tcp:Url=http://127.0.0.1:0"
    ];

    private static string GetAddress(WebApplication app) =>
        app.Services
            .GetRequiredService<IServer>()
            .Features
            .Get<IServerAddressesFeature>()!
            .Addresses
            .Single()
            .TrimEnd('/');

    private static void AssertSafePayload(string body)
    {
        Assert.DoesNotContain("supersecret", body, StringComparison.Ordinal);
        Assert.DoesNotContain("secret-db.internal", body, StringComparison.Ordinal);
        Assert.DoesNotContain("Password=", body, StringComparison.Ordinal);
        Assert.DoesNotContain("Host=", body, StringComparison.Ordinal);
        Assert.DoesNotContain("<html", body, StringComparison.OrdinalIgnoreCase);
    }

    private sealed class StubReadinessProbe : IStorageReadinessProbe
    {
        private readonly StorageReadinessReport _report;

        public StubReadinessProbe(StorageReadinessReport report)
        {
            _report = report;
        }

        public Task<StorageReadinessReport> ProbeAsync(CancellationToken cancellationToken) =>
            Task.FromResult(_report);
    }

    private sealed class ThrowingReadinessProbe : IStorageReadinessProbe
    {
        private readonly Exception _exception;

        public ThrowingReadinessProbe(Exception exception)
        {
            _exception = exception;
        }

        public Task<StorageReadinessReport> ProbeAsync(CancellationToken cancellationToken) =>
            Task.FromException<StorageReadinessReport>(_exception);
    }
}
