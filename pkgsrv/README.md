# Pkgsrv

Pkgsrv is a framework-independent DDD skeleton for package registration, listing,
and build requests. It uses the deployed Ervan mediator setup exactly:
`Arlirad.Ervi.Mediator` 1.1.0, target `net10.0`, and `AddMediator(typeof(...))`.

The implementation is safe-by-default. It does not execute builds, does not start
processes, does not publish unsigned artifacts, and does not claim real Irva
authorization. All dangerous capabilities are ports with non-executing or denying
adapters.

## Layers

| Project | Responsibility | Dependencies |
| --- | --- | --- |
| `src/Pkgsrv.Domain` | Package aggregate, value objects, manifest, build policy, domain events, invariants | BCL only |
| `src/Pkgsrv.Application` | Commands/queries, handlers, ports, DTOs, validation, authorization intent, permission names | Domain, Ervi mediator contracts |
| `src/Pkgsrv.Infrastructure` | In-memory and PostgreSQL package stores, source allowlist, safe build sandbox, artifact/signing adapters, pending Irva boundary | Application, Domain |
| `src/Pkgsrv.Presentation` | Minimal HTTP API, request/response DTOs, actor context propagation, error mapping, composition root | Application, Infrastructure, Domain |
| `tests/*` | Layer tests; `test-support` holds shared valid fixtures | xUnit |

`Pkgsrv.sln` is at the root of this directory. It targets `net10.0` to match the
deployed Ervan server and the current Irva.Client project. It was built with
.NET SDK 10.0.114 against the .NET 10 runtime.

## Resolved Ervan and Irva sources

Deployment inspection resolved the real sources outside this worktree:

- Deployed Ervan root: `/home/tymk/Projects/_prg/servers/ervan`
- Vendored Irva checkout inside Ervan: `/home/tymk/Projects/_prg/servers/ervan/lib/irva`
- Main Irva checkout: `/home/tymk/Projects/_prg/servers/irva`
- Deployed Ervan target: pacman service `ervan`
- Ervan layout: `src/Ervan.Core`, `src/Ervan.Application`, `src/Ervan.Infrastructure`,
  `src/Ervan.Presentation.Api`, plus protocol/transport/import projects
- Ervan mediator registration: `services.AddMediator(typeof(Server))`
- Ervan references `Arlirad.Ervi.Mediator` 1.1.0

Ervan's API project references Irva.Client with the local project path
`../../lib/irva/src/Irva.Client/Irva.Client.csproj`. Irva.Client is a `net10.0`
project, not a public NuGet package.

This worktree does not reference that out-of-worktree project. A hard project
reference would make pkgsrv dependent on an external checkout and a move would
break restore. Instead, integration is behind the compile-time boundary described
below.

## Mediator integration

No parallel mediator is defined. Handlers implement the real Ervi contracts:

- `Arlirad.Ervi.Mediator.Interfaces.IRequest<TResponse>`
- `Arlirad.Ervi.Mediator.Interfaces.IRequestHandler<TRequest, TResponse>`
- `ValueTask<TResponse> Handle(TRequest request, CancellationToken ct)`

The composition root uses the same pattern as deployed Ervan:

```csharp
services.AddMediator(typeof(RegisterPackageCommandHandler));
```

`AddMediator` scans the Application assembly and registers every request handler.
HTTP handlers and tests use `IMediator.Send(...)` directly. Ervi's reflection
mediator wraps handler exceptions in `TargetInvocationException`; the presentation
error mapper unwraps that before mapping domain/application failures to HTTP.

Replace the root marker type with the real composition type when hosting pkgsrv as
a module inside Ervan.

## Irva authorization boundary and permissions

`Application.Abstractions.Security.IIrvaAuthorization` expresses the read, build,
and publish policy intent. Every application handler authorizes before doing work:

| Handler | Action | Permission name |
| --- | --- | --- |
| registration | `Publish` | `publish` |
| listing | `Read` | `read` |
| build request | `Build` | `build` |

The permission names are constants in
`Pkgsrv.Application.Authorization.PackagePermissions` and match the flat,
app-local permission style used by Ervan's `irva.yaml`. They map directly to
`IrvaClient.HasPermission(accountId, fullName)`.

An `irva.yaml` application entry for pkgsrv can declare:

```yaml
permissions:
  read:
  build:
  publish:
```

The real Irva.Client is not available in this repository. The infrastructure
therefore supplies:

- `IIrvaClientAuthorizationBoundary` - the compile-time seam for a future
  Irva.Client integration
- `PendingIrvaClientAuthorizationBoundary` - reports the integration as unavailable
- `PendingIrvaClientAuthorizationAdapter` - converts unavailable or denied boundary
  results into a deny; it never fabricates claims or grants

With the default composition, every authorization decision is denied. Tests and
future integrations can replace `IIrvaAuthorization` in DI.

### Pending Irva.Client wiring

A future adapter should wrap the resolved Irva.Client API:

```csharp
new IrvaClient(new IrvaClientOptions
{
    BaseUrl = baseUrl,
    AppToken = appToken,
    SocketAddress = socketAddress
});

VerifyTokenResponse verified = await appClient.VerifyAsync(accessToken, ct);
bool allowed = appClient.HasPermission(verified.AccountId, permissionName);
```

The deployed Ervan `IrvaAuthService` follows this shape with an app client and a
public client, catches `IrvaApiException`, and checks permissions after
`VerifyAsync`. The pkgsrv boundary should do the same and return
`IrvaClientAuthorizationResponse.Unavailable` or `Denied` on failure.

`AuthorizationContext.AccountId` is the intended carrier for the verified Irva
account id. The presentation layer reads `X-Pkgsrv-Actor`, `X-Pkgsrv-Roles`, and
`X-Pkgsrv-Account` as transport context only. This is not authentication, there is
no authentication middleware, and the headers must be replaced by real Irva
identity propagation when wired into Ervan.

## Safety guarantees

- No request DTO or command has a shell, command, script, or executable field.
- `NonExecutingBuildSandbox` always returns `NotExecuted` and never starts a process.
- `RefusingUnsignedPackagePublisher` refuses pending or unsigned manifests and
  unsigned artifact metadata. Only a signed manifest plus a signed artifact for the
  same package identity with a matching content hash can pass.
- Package build-request transitions are atomic under concurrent requests; one
  transition wins, raises one domain event, and the rest are rejected.
- Domain events are published from a stable snapshot and acknowledged only after
  publication succeeds; a publisher failure retains the events on the aggregate.
- `PendingPackageSigner` returns a pending signature; it does not sign.
- `ConfiguredSourceAllowlist` denies everything unless hosts are configured.
  Wildcards such as `*.example.com` are supported.
- `InMemoryPackageRepository` is process-local and not durable.
- `PostgresPackageRepository` makes package metadata durable and keeps duplicate
  registration and concurrent build-request transitions serialized in PostgreSQL.
  Domain event publication remains in-process.

## Package storage and schema

Package metadata is durable when PostgreSQL is configured. One
`PostgresPackageRepository` implements both `IPackageRepository` and `IPackageIndex`; it
rehydrates the existing domain aggregate directly from relational columns and does not
replay domain events.

Configuration under `Pkgsrv:Storage`:

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Storage__Provider` | inferred | `InMemory` or `Postgres`. When absent, PostgreSQL is selected if a connection string is present; otherwise in-memory is selected. |
| `Pkgsrv__Storage__PostgresConnectionString` | none | Npgsql connection string. Required when the provider is `Postgres`. |
| `Pkgsrv__Storage__MaxListResults` | `1000` | Hard upper bound (1-10000) applied to list/search SQL. |

With no configured connection string, composition selects the explicit in-memory adapter for
tests and local development. That adapter is not durable. Selecting `Postgres` without a
connection string fails configuration validation. When PostgreSQL is selected, startup
verifies connectivity and `pkgsrv.schema_versions`; an unreachable database, a missing
schema, or a schema version older than 1 fails startup. Startup never creates, resets, or
drops tables.

Apply the versioned schema explicitly:

```bash
psql "$PKGSRV_POSTGRES_CONNECTION" -f pkgsrv/db/migrations/0001_packages.sql
```

The script creates `pkgsrv.schema_versions` and `pkgsrv.packages`. Package identity uses a
`(name, version)` primary key. Manifest, build-policy, state, timestamps, and signature and
provenance fields are durable relational columns; no `jsonb` is used. Indexes support
deterministic list ordering and name/source-commit search. Search SQL is parameterized and
escapes `LIKE` wildcards in user input. List results are ordered deterministically by name,
numeric major/minor/patch, pre-release text with releases after pre-releases, build-metadata
text, and canonical version, and are capped by `MaxListResults`. Full SemVer identifier
precedence and version-aware search are tracked as a residual. Registration uses `INSERT ...
ON CONFLICT DO NOTHING`; build-state updates use a transaction with `SELECT ... FOR UPDATE`
before the conditional update, so duplicate and concurrent build requests keep their existing
rejection semantics. A stale registered aggregate cannot overwrite a persisted build-requested
state.

A live PostgreSQL integration suite is not part of the default test run because the
repository has no testcontainer dependency. Row mapping, SQL shape, search parameter
escaping, and the migration script are tested without a database. A live run requires a
provisioned PostgreSQL instance and the migration above.

## Read-model cache

GET detail and list results are cached as serialized JSON read models at the application
boundary. Cache lookups happen after validation and authorization; authorization decisions
and errors are never cached. Successful registration and build-state updates invalidate the
package detail key and every list key. A database read captures a cache generation before it
starts, and a fill is discarded if any invalidation occurs before the fill commits, so a read
started before a write cannot repopulate stale data after the invalidation.

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Cache__MaxEntries` | `1024` | Hard entry bound. Expired entries are removed first, then least-recently-used entries. |
| `Pkgsrv__Cache__AbsoluteExpirationSeconds` | `300` | Absolute lifetime after insertion. |
| `Pkgsrv__Cache__SlidingExpirationSeconds` | `60` | Inactivity lifetime. Hits extend it, capped by the absolute lifetime. |

The cache is process-local. Other pkgsrv processes observe changes after expiration or after
their own writes. PostgreSQL remains the source of truth, and HTTP JSON responses are still
generated per request by ASP.NET Core. Cache entries contain immutable DTO JSON, not mutable
domain aggregates.

## Listening endpoint

The production default is a Unix domain socket:

```
unix:/run/pkgsrv/pkgsrv.sock
```

Kestrel binds only that socket by default, so no TCP port is opened. The path and
the explicit TCP override are configured under `Pkgsrv:Listen`:

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Listen__UnixSocketPath` | `/run/pkgsrv/pkgsrv.sock` | Absolute socket path |
| `Pkgsrv__Listen__Tcp__Enabled` | `false` | Replaces the Unix socket with TCP when `true` |
| `Pkgsrv__Listen__Tcp__Url` | `http://127.0.0.1:5080` | HTTP loopback URL, used only when TCP is enabled |

TCP is an explicit local/dev override, not an additional listener. Non-loopback
hosts, HTTPS, and URL paths are rejected. When TCP is enabled, the Unix socket is
not bound. Kestrel URL settings such as `ASPNETCORE_URLS` and `--urls` are
intentionally cleared so only the settings above control the listener.

Configuration uses the standard ASP.NET Core chain from
`WebApplication.CreateBuilder`. The highest-priority source wins, with this
precedence:

1. command line
2. environment variables
3. `appsettings.{DOTNET_ENVIRONMENT}.json`
4. `appsettings.json`
5. built-in defaults shown above

For example, the command line wins over the environment:

```bash
Pkgsrv__Listen__UnixSocketPath=/tmp/from-env/pkgsrv.sock \
dotnet run --project pkgsrv/src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj -- \
  --Pkgsrv:Listen:UnixSocketPath=/tmp/from-cli/pkgsrv.sock
```

Only a missing key selects the built-in default. An explicitly supplied empty or
whitespace value for `Pkgsrv:Listen:UnixSocketPath`, `Pkgsrv:Listen:Tcp:Enabled`,
or `Pkgsrv:Listen:Tcp:Url` fails startup validation instead of silently falling
back.

For local development without a provisioned `/run/pkgsrv`:

```bash
Pkgsrv__Listen__Tcp__Enabled=true \
Pkgsrv__Listen__Tcp__Url=http://127.0.0.1:5080 \
dotnet run --project pkgsrv/src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj
```

The application does not create `/run/pkgsrv` or attempt privileged directory
creation. The directory must already exist and be writable by the service user, or
startup fails. Under systemd, use `RuntimeDirectory` so systemd creates and removes
it with the service:

```ini
[Service]
User=pkgsrv
Group=pkgsrv
RuntimeDirectory=pkgsrv
RuntimeDirectoryMode=0750
UMask=0007
ExecStart=/usr/bin/dotnet /usr/share/pkgsrv/Pkgsrv.Presentation.dll
WorkingDirectory=/usr/share/pkgsrv
```

The socket is owned by the service user and group. A reverse proxy must be able to
traverse the runtime directory and connect to the socket, typically by joining the
`pkgsrv` group. `UMask=0007` makes the socket group-readable/writable; `0750` keeps
the directory private to the service group.

Example nginx upstream:

```nginx
upstream pkgsrv {
    server unix:/run/pkgsrv/pkgsrv.sock;
}
```

Example Caddy target:

```caddyfile
reverse_proxy unix//run/pkgsrv/pkgsrv.sock
```

## HTTP API

| Method | Route | Result |
| --- | --- | --- |
| `GET` | `/healthz` | `200` health marker |
| `POST` | `/packages` | `201` registered package, `400`/`403`/`409`/`422` on failure |
| `GET` | `/packages` | `200` package summaries |
| `GET` | `/packages/{name}/{version}` | `200` package details; location returned by registration |
| `POST` | `/packages/{name}/{version}/build` | `202` with `NotExecuted`, or `403`/`404`/`409` |

Registration request fields:

```
name, version, sourceUrl, sourceCommit, toolchainName, toolchainVersion,
contentHash, provenance, signature, signatureAlgorithm, isolation,
allowNetwork, timeoutSeconds
```

`isolation` is `Namespace`, `Container`, or `VirtualMachine`. Registration accepts
a pending signature; publication does not.

Configure source hosts through configuration, for example:

```bash
export Pkgsrv__Sources__AllowedHosts__0=github.com
export Pkgsrv__Sources__AllowedHosts__1='*.example.com'
```

With no configured hosts, registration is rejected by the allowlist.

## Build and test

Prerequisites: .NET 10 SDK/runtime and NuGet access (or a populated package cache
containing `Arlirad.Ervi.Mediator` 1.1.0 and xUnit). The installed ASP.NET Core 10
targeting pack lacks prune-package data, so `Directory.Build.props` sets
`AllowMissingPrunePackageData=true` for offline builds.

```bash
dotnet --info
dotnet build pkgsrv/Pkgsrv.sln
dotnet build pkgsrv/Pkgsrv.sln --no-restore
dotnet test pkgsrv/Pkgsrv.sln
```

Run the API on the default Unix socket (the runtime directory must exist):

```bash
dotnet run --project pkgsrv/src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj
```

For local TCP instead, enable the explicit loopback override as shown in the
listening endpoint section.

## Known limitations

- Package download, build execution, signing, and publish endpoints are not implemented.
- Durable metadata requires a provisioned PostgreSQL database and the schema migration;
  the default test/dev composition is in-memory only.
- Domain events remain in-process and are not written to a transactional outbox, so an
  event-publisher failure after a successful metadata write is not replayed automatically.
- The read-model cache is process-local; multiple instances do not share invalidation.
- Contains-search over provenance is not trigram-indexed, so very large package tables may
  need a PostgreSQL `pg_trgm` index chosen for the deployment.
- Irva.Client authorization is pending and defaults to deny.
- No project reference is made to `/home/tymk/Projects/_prg/servers/ervan`; the
  permission and client adapter must be wired when hosting alongside Ervan.
- Package build requests mutate durable state when PostgreSQL is configured and return
  `NotExecuted`; this is deliberate until a verified sandbox adapter exists.
- The application deliberately does not create the Unix socket runtime directory.

## Tracked residuals

- [#174](https://github.com/arlirad/astia/issues/174) - live PostgreSQL integration and
  migration conformance tests.
- [#175](https://github.com/arlirad/astia/issues/175) - cross-instance read-model cache
  invalidation.
- [#176](https://github.com/arlirad/astia/issues/176) - transactional outbox for package
  domain events.
- [#177](https://github.com/arlirad/astia/issues/177) - provenance contains-search index or
  restricted search fields.
- [#178](https://github.com/arlirad/astia/issues/178) - real Irva authorization and Ervan
  hosting boundary.
- [#179](https://github.com/arlirad/astia/issues/179) - sandboxed build execution, signing,
  and package delivery.
- [#180](https://github.com/arlirad/astia/issues/180) - storage mode and PostgreSQL
  availability in health/readiness.
- [#197](https://github.com/arlirad/astia/issues/197) - full SemVer precedence and
  version-aware search.
