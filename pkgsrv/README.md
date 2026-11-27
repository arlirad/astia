# Pkgsrv

Pkgsrv is a framework-independent DDD skeleton for package registration, listing,
and build requests. It uses the deployed Ervan mediator setup exactly:
`Arlirad.Ervi.Mediator` 1.1.0, target `net10.0`, and `AddMediator(typeof(...))`.

The implementation is safe-by-default. It does not execute builds unless an operator
explicitly enables an allowlisted build profile and runtime, does not start
unconfined processes, does not publish unsigned artifacts, and denies all package
requests unless Irva bearer authentication and authorization are explicitly wired.
All dangerous capabilities are ports with non-executing or denying adapters.

## Layers

| Project | Responsibility | Dependencies |
| --- | --- | --- |
| `src/Pkgsrv.Domain` | Package aggregate, value objects, manifest, build policy, domain events, invariants | BCL only |
| `src/Pkgsrv.Application` | Commands/queries, handlers, ports, DTOs, validation, authorization intent, permission names | Domain, Ervi mediator contracts |
| `src/Pkgsrv.Infrastructure` | In-memory and PostgreSQL package stores, source allowlist, safe build sandbox, artifact/signing adapters, Irva.Client authorization boundary and optional real adapter | Application, Domain |
| `src/Pkgsrv.Presentation` | Minimal bearer-authenticated HTTP API, Irva identity middleware, JSON read cache, request/response DTOs, error mapping, composition root | Application, Infrastructure, Domain |
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

## Irva authentication and authorization

Pkgsrv is a bearer-token resource server. It does **not** expose login,
registration, session, refresh-token, token-proxy, or password endpoints. Irva owns
identity, login, registration, sessions, and token issuance. Every pkgsrv package
endpoint requires `Authorization: Bearer <Irva access token>`; only `GET /healthz`
and `GET /readyz` are unauthenticated.

Request flow:

1. The browser or client authenticates with Irva and stores the Irva access token.
2. The client calls pkgsrv with that token in the `Authorization: Bearer` header.
3. `IrvaAuthenticationMiddleware` sends the token to Irva through
   `IrvaClient.VerifyAsync(accessToken, ct)`.
4. The verified Irva `AccountId` and `Username` are stored in the request-scoped
   `AuthorizationContext`. Client-supplied `X-Pkgsrv-Actor`, `X-Pkgsrv-Roles`, and
   `X-Pkgsrv-Account` headers are ignored and never authorize anything.
5. `IrvaClientAuthorizationBoundary` checks the verified account against the
   required `read`, `build`, or `publish` permission through
   `IrvaClient.HasPermission(accountId, fullName)`.
6. Missing or invalid bearer tokens return `401`; a verified account without the
   permission returns `403`; an unavailable or incomplete Irva integration fails
   closed and does not silently allow the request.

Raw bearer tokens are never logged, included in DTOs, or used in cache keys. The
JSON read cache keys on the verified `AuthorizationContext`, not on client headers.

### Permissions and `irva.yaml`

Every application handler authorizes before doing work:

| Handler | Action | Permission full name |
| --- | --- | --- |
| registration | `Publish` | `publish` |
| listing | `Read` | `read` |
| package details | `Read` | `read` |
| build request | `Build` | `build` |
| publication | `Publish` | `publish` |
| download | `Read` | `read` |

The names are constants in `Pkgsrv.Application.Authorization.PackagePermissions` and
map directly to `IrvaClient.HasPermission(accountId, fullName)`. An `irva.yaml`
application entry for pkgsrv declares the flat permission tree:

```yaml
permissions:
  read:
  build:
  publish:
```

Grant those full names to the users or roles that should use each operation. Pkgsrv
does not maintain roles, accounts, or permissions itself.

### Operator configuration

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Irva__Enabled` | `false` | Enables the real Irva verification path |
| `Pkgsrv__Irva__Required` | `false` | When true, startup fails if the integration is incomplete or unwired |
| `Pkgsrv__Irva__BaseUrl` | unset | Irva API base URL |
| `Pkgsrv__Irva__AppToken` | unset | Irva application token used by the app client; never logged |
| `Pkgsrv__Irva__SocketAddress` | unset | Optional absolute Unix domain socket path |

```bash
export Pkgsrv__Irva__Enabled=true
export Pkgsrv__Irva__Required=true
export Pkgsrv__Irva__BaseUrl=https://irva.example
export Pkgsrv__Irva__AppToken=REDACTED
# Optional:
export Pkgsrv__Irva__SocketAddress=/run/irva/irva.sock
```

Validation rules:

- `Required=true` requires `Enabled=true`.
- `BaseUrl` must be an absolute HTTP(S) URL with no credentials, query, or fragment.
- Non-HTTPS is accepted only for loopback hosts or when an explicit Unix socket
  address is configured, matching `IrvaClientOptions`.
- `SocketAddress` must be an absolute filesystem path.
- With `Enabled=false`, or with enabled-but-incomplete/unwired settings, all
  authorization decisions fail closed. With `Required=true`, an incomplete or
  unwired enabled integration fails startup instead of serving.

### Real Irva.Client API and optional build wiring

The deployed Irva.Client project is external to this repository:

```text
/home/tymk/Projects/_prg/servers/irva/src/Irva.Client/Irva.Client.csproj
/home/tymk/Projects/_prg/servers/ervan/lib/irva/src/Irva.Client/Irva.Client.csproj
```

The API used by the adapter is:

```csharp
new IrvaClient(new IrvaClientOptions
{
    BaseUrl = baseUrl,
    AppToken = appToken,
    SocketAddress = socketAddress
});

VerifyTokenResponse verified = await irvaClient.VerifyAsync(accessToken, ct);
bool allowed = irvaClient.HasPermission(verified.AccountId, "read");
```

`VerifyAsync` returns the verified `AccountId` and `Username` and populates the
client permission cache; `HasPermission` is checked immediately afterwards against
the verified account id. The adapter maps Irva 4xx verification failures to
"invalid token" (`401` at the pkgsrv edge) and network, timeout, 5xx, or malformed
responses to "authorization unavailable" (`503`, fail closed).

The external project is not referenced by the default solution build. The real
adapter is a conditional source file selected by the `IrvaClientProject` MSBuild
property. Default builds compile the safe deny path and never depend on an external
checkout. To compile and test the real adapter from an operator environment:

```bash
dotnet restore pkgsrv/Pkgsrv.sln \
  -p:IrvaClientProject=/path/to/irva/src/Irva.Client/Irva.Client.csproj
dotnet build pkgsrv/Pkgsrv.sln --no-restore \
  -p:IrvaClientProject=/path/to/irva/src/Irva.Client/Irva.Client.csproj
dotnet test pkgsrv/Pkgsrv.sln \
  -p:IrvaClientProject=/path/to/irva/src/Irva.Client/Irva.Client.csproj
```

Without that property, `IrvaClientAdapterFactory.IsAvailable` is false. If
`Enabled=true` and `Required=false`, the integration remains fail-closed and returns
`authorization.unavailable`; if `Required=true`, composition fails clearly. It does
not fall back to the pending adapter, in-memory claims, or header-derived identity.

An opt-in live integration test runs only when the external adapter is built and
these variables are set; otherwise it is skipped:

```bash
export PKGSRV_TEST_IRVA_BASE_URL=https://irva.example
export PKGSRV_TEST_IRVA_APP_TOKEN=REDACTED
export PKGSRV_TEST_IRVA_ACCESS_TOKEN=REDACTED
# Optional:
export PKGSRV_TEST_IRVA_SOCKET_ADDRESS=/run/irva/irva.sock
```

### Ervan hosting boundary

Deployed Ervan resolves Irva through `Ervan.Infrastructure.Services.IrvaAuthService`:
it constructs an app-token `IrvaClient` for `VerifyAsync`/`HasPermission` and a
public `IrvaClient` for user login/attest flows, then catches `IrvaApiException`.
Pkgsrv follows the same app-client pattern and does not create a second identity
store.

When hosting pkgsrv as a module beside Ervan:

- share the same Irva application registration and permission tree (`read`,
  `build`, `publish`), not Ervan's `send`/`view` names;
- build pkgsrv with the same Irva.Client project reference path used by Ervan, or
  register an `IIrvaClientAdapter` over the existing app-token `IrvaClient`;
- keep the public user-login client in Irva/Ervan; pkgsrv never accepts passwords or
  issues tokens;
- do not place the app token in source control, logs, DTOs, or client bundles.

No hard project reference to `/home/tymk/Projects/_prg/servers/ervan` is committed;
a deployed composition must supply the external path explicitly.

### Astia Web integration contract

Astia Web's `nodivia.frontend` already owns Irva token storage and authentication.
Its existing `IrvaTokenStorage` plus global bearer interceptor with single-flight
refresh is the client contract:

- Pkgsrv package requests must **not** call `skipAuthContext()`. Use the existing
  Irva bearer interceptor so `Authorization: Bearer <token>` is attached.
- Do not hardcode a pkgsrv host or token in the frontend. Use an injectable
  `PKGSRV_BASE` or a relative same-origin path such as `/packages`.
- Never send `X-Pkgsrv-Actor`, `X-Pkgsrv-Roles`, or `X-Pkgsrv-Account`; pkgsrv
  ignores them for authorization.
- Pkgsrv exposes only the resource API: `GET /healthz`, `GET /readyz`,
  `GET /packages`, `GET /packages/{name}/{version}`, `POST /packages`,
  `POST /packages/{name}/{version}/build`,
  `POST /packages/{name}/{version}/publish`, and
  `GET /packages/{name}/{version}/download`. There are no pkgsrv login, register,
  refresh, or proxy-auth routes.
- Same-origin reverse proxying to pkgsrv is preferred. For cross-origin use, CORS
  must allow the exact origin, `Authorization`, and the required methods/headers,
  including preflight `OPTIONS`; pkgsrv binds a local Unix socket or loopback TCP by
  default, so the deployment proxy is the public boundary.
- A `401` with code `authorization.unauthenticated` means missing or invalid bearer
  identity: refresh once through Irva, retry, and otherwise send the user to Irva
  login. A `403` with code `authorization.denied` means the account is authenticated
  but lacks `read`, `build`, or `publish`; do not retry or prompt for login. A `503`
  with code `authorization.unavailable` means Irva or pkgsrv authorization is
  temporarily unavailable; fail closed and retry/back off rather than treating it
  as success.
- Error responses are stable RFC 7807 `application/problem+json` documents with
  `type`, `title`, `status`, `detail`, and the pkgsrv `code` extension. Success
  responses keep the existing package JSON/binary contracts.

The server remains the authority for identity and permissions. Astia Web must not
derive authorization from local token contents or client headers.

## Safety guarantees

- Package endpoints require a verified Irva bearer identity and the matching
  `read`/`build`/`publish` permission. Missing, invalid, or unavailable identity and
  permission denials fail closed; client identity headers are ignored.
- No request DTO or command has a shell, command, script, executable, output path,
  or arbitrary build-arguments field.
- Build execution is disabled unless `Pkgsrv:Build:Enabled=true`, an allowlisted
  profile matches the package toolchain, and the configured bubblewrap runtime
  exists. Unsupported isolation modes return `NotExecuted`; there is no host-process
  fallback.
- The bubblewrap adapter uses fixed argv only. Client data is never shell-interpolated.
- Source checkout uses fixed `git` argv pinned to the registered commit, rechecks
  the source allowlist, and removes the temporary workspace.
- Build network access is denied by default and enabled only by the registered
  build policy; timeouts kill the process tree.
- `NonExecutingBuildSandbox` remains the default when build execution is not
  explicitly configured.
- `VerifyingPackagePublisher` requires a signed, verifiable manifest and matching
  content-addressed artifact bytes. `RefusingUnsignedPackagePublisher` remains the
  safe default when signing is not configured.
- Package build-request transitions are atomic under concurrent requests; one
  transition wins, raises one domain event, and the rest are rejected.
- A package can transition to durable `Published` only after publication succeeds;
  the transition is a conditional write requiring the expected build state, hash,
  and signature. Downloads refuse until that marker is persisted.
- In PostgreSQL mode package registration/build state and its supported domain
  events are committed in one transaction into the package row and the
  `package_outbox` table. A crash cannot commit a package without its outbox rows.
  In-memory mode keeps the synchronous in-memory publisher and has no outbox.
- A PostgreSQL background dispatcher claims outbox rows with row locks, publishes
  through an injected event publisher adapter, and marks a row delivered only after
  publication succeeds. Failed deliveries retain the row, retry with bounded
  exponential backoff, and stop at the configured maximum attempt count. Delivery
  is at-least-once; every outbox row exposes a stable idempotency key and consumers
  must deduplicate on it.
- PostgreSQL build transitions use a conditional row update requiring the persisted
  state to still be `Registered`; a stale transition raises the 409 conflict path.
- `PendingPackageSigner` returns a pending signature; it does not sign.
- `ConfiguredSourceAllowlist` denies everything unless hosts are configured.
  Wildcards such as `*.example.com` are supported.
- `InMemoryPackageRepository` is process-local and not durable; PostgreSQL mode is
  durable when configured with a connection string.

## Persistence

Package metadata is stored through `IPackageRepository` and `IPackageIndex`. The
production-capable adapter uses EF Core 10 with Npgsql and the `packages` table.

Configuration:

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Database__Provider` | `InMemory` fallback | `InMemory` or `Postgres`/`PostgreSQL` |
| `Pkgsrv__Database__ConnectionString` | unset | PostgreSQL connection string; selects Postgres when set |
| `Pkgsrv__Database__InitializeSchema` | `false` | Applies pending EF Core migrations at startup when true |

Selection is deterministic:

- A non-empty connection string with no explicit provider selects PostgreSQL.
- `Provider=InMemory` explicitly selects the non-durable in-memory adapter.
- No provider and no connection string uses the in-memory fallback; startup logs a
  warning that metadata is not durable.
- `Provider=Postgres` requires a non-empty connection string.

Example using placeholders only:

```bash
export Pkgsrv__Database__Provider=Postgres
export Pkgsrv__Database__ConnectionString='Host=db.example;Database=pkgsrv;Username=pkgsrv;Password=REDACTED'
export Pkgsrv__Database__InitializeSchema=false
```

The adapter maps package key, all manifest fields, build policy, aggregate state,
and registration/build timestamps. Build timeouts are stored as ticks, so fractional
`TimeSpan` values survive a round trip. Rehydration uses a domain factory and does
not raise registration or build events.

Build requests use a conditional row update requiring the persisted state to still
be `Registered`. Concurrent build requests race on that predicate; one succeeds,
and a stale update — including a stale `Registered` aggregate attempting to
overwrite a persisted `BuildRequested` state — raises `PackageConflictException`,
mapped to HTTP `409`.

Publication state is persisted as `State=Published` plus `PublishedAt`. The
publication transition is a conditional update requiring the prior row to still be
`BuildRequested`, with the expected signed manifest, content hash, and signature
value. Successful retries already marked `Published` with the same hash/signature
complete idempotently; ambiguous state or hash/signature mismatch maps to the
`409` conflict path. A package is never marked published before the publisher
succeeds.

List results are ordered deterministically by name, numeric major/minor/patch,
full SemVer 2.0 pre-release precedence, and canonical version. Pre-release
identifiers compare numerically when numeric, lexically in ASCII order when
alphanumeric, numeric identifiers sort below alphanumeric identifiers, shorter
identifier sets sort first when prefixes match, and releases sort after every
pre-release. Build metadata does not affect precedence; versions that compare
equal by precedence are ordered by canonical version text. PostgreSQL orders by a
persisted pre-release sort key produced by the same encoder as the in-memory
comparer. Numeric identifiers with leading zeroes are outside SemVer and are
encoded as alphanumeric text, so invalid input still has one deterministic order.
Filtering, ordering, escaping, and the 1000-entry cap are applied in SQL before
materialization. Search supports the bounded fields: case-insensitive package
`name` contains, canonical `version` contains including pre-release and
build-metadata text, and source commit prefix. Filtering remains bounded in SQL,
but none of these fields is claimed as indexed unless verified in the current EF
migration/schema. Provenance and other manifest metadata are returned but are not
search fields; provenance containment search requires a reviewed trigram/index
strategy ([#177](https://github.com/arlirad/astia/issues/177)). Search treats `%` and
`_` in user input literally by escaping them, matching the in-memory
`Contains`/`StartsWith` behavior.

PostgreSQL registration and build transitions use a transactional outbox. The
package row (including the existing `PendingEventsJson` compatibility payload) and
one `package_outbox` row per supported domain event are written in the same EF Core
transaction. `PendingEventsJson` is retained so existing durable pending-event data
is not dropped; the outbox rows are the dispatch source. Only the supported
`PackageRegistered` and `PackageBuildRequested` events are serialized, including
their manifest/build-policy fields and no authorization tokens or connection
secrets. Payload JSON is deterministic.

The outbox table uses a stable idempotency key as its primary key: event type,
package name/version, and occurrence timestamp. Replaying the same semantic event
inserts nothing (`ON CONFLICT DO NOTHING`), so duplicate writes do not create
duplicate rows. A PostgreSQL `PackageOutboxDispatcher` hosted service polls in
bounded batches, claims pending rows with `FOR UPDATE SKIP LOCKED` plus a lease,
publishes after commit through the configured publisher adapter, and marks rows
delivered only after success. Failures retain the row and retry with bounded
exponential backoff up to a maximum attempt count; exhausted rows remain durably
`Failed`. Restart or a crash after claim is recovered after the lease expires.

This is an **at-least-once durable transactional outbox**. Consumers must
deduplicate on the exposed idempotency key. A publication failure after the atomic
commit cannot roll back the package write and does not fail the package request;
the row remains for the dispatcher. In-memory mode has no outbox table or hosted
dispatcher and keeps its simple synchronous publisher for development and tests.

## Health and readiness

`GET /healthz` is process liveness only. It returns `200` with
`{"status":"ok"}` and never opens a database connection. Use it to decide whether
the process is running, not whether package storage is usable.

`GET /readyz` reports storage readiness with an additive `application/json`
payload:

```json
{
  "status": "ready",
  "storageProvider": "InMemory",
  "storageDurable": false,
  "schemaVersion": null,
  "postgresAvailable": null
}
```

- `status` is `ready` or `not_ready`.
- `storageProvider` is `InMemory` or `Postgres`.
- `storageDurable` is `false` for the process-local in-memory store and `true` for
  PostgreSQL configuration.
- `schemaVersion` is the number of applied EF Core migrations when migration state
  was read successfully; it is `null` when PostgreSQL is unreachable or the
  in-memory provider is selected.
- `postgresAvailable` is `null` for the in-memory provider, `true` when the
  PostgreSQL probe connected, and `false` when the probe timed out, failed, or
  found the database unavailable.

In-memory mode returns `200` and is explicitly non-durable. PostgreSQL mode returns
`200` only when a bounded probe connects, reads `__EFMigrationsHistory`, finds no
pending migrations, and the applied migration set exactly matches the migrations
known to this build. Pending, unknown, unavailable, timed-out, or failed migration
state returns `503` with the same safe payload. The response never contains a
connection string, hostname, credential, filesystem path, mount source, or exception
message. If PostgreSQL is selected but the readiness probe is not registered,
cannot be resolved, or throws, `/readyz` returns `503` JSON instead of an HTML
error or `500`.

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Readiness__PostgresProbeTimeoutMs` | `2000` | Bounded PostgreSQL probe timeout in milliseconds, accepted range `1`–`10000` |

Each probe creates and disposes a fresh `DbContext`, uses a linked timeout token,
and holds no long-lived connection. Startup migration behavior is unchanged: with
`InitializeSchema=true` startup applies migrations, and with `false` startup fails
before listening when migrations are pending or migration state is unavailable.

Monitoring should use `/readyz` for durable availability and `/healthz` only for
process liveness. Both routes are unauthenticated and are reached through the same
same-origin reverse proxy as the rest of the API, for example
`https://pkgsrv.example/readyz`.

## Sandboxed build execution

Build execution is opt-in and uses bubblewrap for the only currently supported
isolation mode, `Namespace`. `Container` and `VirtualMachine` requests return
`NotExecuted` with an explicit unsupported-mode detail; they never fall back to a
host process.

```bash
export Pkgsrv__Build__Enabled=true
export Pkgsrv__Build__AcceptNamespaceOnlyWithoutResourceLimits=true
export Pkgsrv__Build__BubblewrapPath=/usr/bin/bwrap
export Pkgsrv__Build__WorkspaceRoot=/run/pkgsrv/builds
export Pkgsrv__Build__WorkspaceQuotaBytes=268435456
export Pkgsrv__Build__WorkspaceQuotaInodes=1000000
export Pkgsrv__Build__MaxArtifactBytes=268435456
export Pkgsrv__Sources__EgressProxy=http://127.0.0.1:3128
export Pkgsrv__Build__Profiles__0__ToolchainName=gcc
export Pkgsrv__Build__Profiles__0__ToolchainVersion=13.2.0
export Pkgsrv__Build__Profiles__0__Executable=/usr/bin/make
export Pkgsrv__Build__Profiles__0__Arguments__0=all
export Pkgsrv__Build__Profiles__0__OutputFile=out/package.bin
```

Profiles are server/operator-defined and selected only by the package toolchain
name/version. The executable, arguments, and output file are fixed configuration;
no client-supplied command, executable, script, output path, or build argument is
accepted. The output file must be relative and remain inside the temp workspace.
Each profile has an operator-owned `NetworkMode`: `Offline` (the default) or
`EgressProxy`. An offline profile runs only with `--unshare-all`, never
`--share-net`, and a client `allowNetwork=true` request against it is rejected
before checkout. `EgressProxy` profiles are accepted by configuration but currently
return `NotExecuted` before checkout because no enforceable egress-proxy network
namespace policy is implemented. Client build policy cannot grant network access to
an offline profile.
`AcceptNamespaceOnlyWithoutResourceLimits=true` is required before builds can be
enabled: it explicitly accepts bubblewrap Namespace-only isolation without cgroup
CPU/memory limits. This is an admission decision, not a claim of resource-limited
sandboxing.

`Pkgsrv:Build:MaxArtifactBytes` defaults to 256 MiB, is operator-only, must be
positive, and is capped at 1 GiB. Oversized output is rejected before allocation or
artifact storage. Build stdout/stderr are drained but only a bounded prefix/tail up
to 64 KiB per stream is retained; excess output is marked truncated and discarded.

`Pkgsrv:Build:WorkspaceQuotaBytes` and `Pkgsrv:Build:WorkspaceQuotaInodes` are
required when builds are enabled. The byte quota is capped at 64 GiB and the inode
quota at 10,000,000. The workspace root must itself be a dedicated `tmpfs` mount whose mountinfo root is
`/`, whose device is distinct from its immediate parent, and whose total size and
inode count are at or below the configured quotas; both are verified with `statvfs`
at runtime before each build. `ramfs`, bind-mounted subdirectories, plain
directories, and mounts where either the mount identity or either cap cannot be
verified are rejected (`NotExecuted`) rather than used on a best-effort basis. The application
does not create the mount, does not partition the host, and does not enforce a
per-build disk controller.

The executor:

- refuses to run as root and requires a non-root service account;
- requires a pre-provisioned workspace root at `/run/pkgsrv/builds` (or configured
  absolute root); it never creates the root;
- validates every path component from `/` to the root for symlink/reparse points,
  requires root ownership by the service UID and exact mode `0700`, and requires
  the parent runtime directory to be service-owned with no group/other write bits
  (a standard `0755` parent is allowed);
- creates each child workspace atomically with `mkdir(2)` mode `0700` under that
  root using an unpredictable name, treats `EEXIST` as a collision, retries, and
  revalidates owner/mode/path before checkout and again immediately before
  bubblewrap;
- removes only the verified child workspace after use, restoring directory
  permissions and deleting symlinks without following them; if cleanup cannot be
  proven safe, the build fails closed rather than reporting success over a leaked
  workspace;
- rechecks the source allowlist, requiring exact `host:port` entries for non-443
  HTTPS ports and exact entries for IP literals; wildcard entries never match IP
  literals;
- checks out the pinned source commit with fixed `git init --template=/dev/null` /
  `remote add` / `fetch` / `checkout --detach` argv through an operator-configured
  egress proxy (`Pkgsrv:Sources:EgressProxy`); checkout fails closed when that proxy
  is absent;
- disables system/global Git config, credential helpers, hooks, SSH, and non-HTTPS
  transports, sets `http.followRedirects=false` so a fetch cannot follow an HTTPS
  redirect away from the configured source host allowlist, and uses an isolated
  per-workspace `HOME`/`XDG_CONFIG_HOME`;
- runs the fixed profile under bubblewrap with a fresh PID/IPC/UTS/cgroup/user
  namespace and read-only host toolchain paths; uses only `--unshare-all` and
  `--cap-drop ALL`, never `--share-net`;
- does not mount an unbounded `--tmpfs /tmp`; it creates a fresh random `0700`
  directory inside the capped workspace and binds that to `/tmp`, so sandbox temp
  data is backed by the same bounded workspace and is removed with it;
- omits network access unless the registered `allowNetwork` policy is true;
- enforces the registered timeout and kills the process tree on timeout;
- validates every output path component for symlink/reparse escapes, opens the
  output with `O_NOFOLLOW | O_NONBLOCK` and `fstat`, and requires a regular file;
  FIFOs, devices, and symlinks are rejected before any read;
- bounds the output read with a deadline and rejects output over the configured size
  limit;
- computes and compares the SHA-256 digest to the expected package hash before
  calling the artifact store; a mismatch never persists bytes;
- stores only verified output content-addressably and verifies the stored hash
  before reporting success.

Bubblewrap does not apply cgroup memory/CPU limits in this adapter. Timeout and
filesystem/namespace isolation are enforced; cgroup resource limits remain an
explicit residual. The workspace quota is a filesystem-size guard, not a per-build
disk I/O controller. Source checkout uses host networking because it occurs before
the sandbox starts, so an operator-provided egress proxy is required to enforce
host/IP egress policy; the source-host allowlist alone does not prevent DNS
rebinding to a private or internal address. If the runtime, profile, isolation
mode, source, workspace root, quota, egress proxy, or operator acknowledgement is
not valid, the result is `NotExecuted` and no process is started.

The default workspace root is a private runtime path and must be provisioned by
systemd/tmpfiles or an equivalent operator action. The application never creates
privileged paths. Example systemd/tmpfiles setup:

```ini
# /etc/systemd/system/run-pkgsrv-builds.mount
[Unit]
Description=Private bounded pkgsrv build workspace

[Mount]
What=tmpfs
Where=/run/pkgsrv/builds
Type=tmpfs
Options=mode=0700,uid=pkgsrv,gid=pkgsrv,size=268435456,nr_inodes=1000000,nosuid,nodev
```

```ini
# unit
[Service]
User=pkgsrv
Group=pkgsrv
RuntimeDirectory=pkgsrv
RuntimeDirectoryMode=0750
RequiresMountsFor=/run/pkgsrv/builds
```

Use `size=` and `nr_inodes=` together so both runtime caps can be verified.
Alternatively use `/etc/tmpfiles.d/pkgsrv-builds.conf` only for a directory that is
already a dedicated tmpfs mount with both caps; tmpfiles cannot create the bounded
mount itself. Do not point the workspace root at a shared `/tmp` or `/run` mount,
a `ramfs` mount, or a tmpfs without both caps.

For local development, create a dedicated tmpfs mount owned by the current
non-root user with mode `0700`, `size=` at or below the configured byte quota, and
`nr_inodes=` at or below the configured inode quota. A plain directory or `ramfs`
mount is rejected by the runtime quota check. Existing unsafe roots and
uncapped roots are rejected rather than chmod-repaired. The runtime directory must
remain service-owned with no group/other write access; the build root itself must
be exactly `0700`.

## Artifacts, signing, and publication

Artifacts are stored under `Pkgsrv:Artifacts:Root` when configured:

```bash
export Pkgsrv__Artifacts__Root=/var/lib/pkgsrv/artifacts
```

If the root is unset, the process-local in-memory artifact store is used and is not
durable. When `Pkgsrv:Artifacts:Root` is set, it must be pre-provisioned by the
operator as a service-owned directory with mode exactly `0700`; the application
does not create it. The root and every ancestor are checked for symlink components.
Package directories are created atomically at `0700`, temporary and final artifact
files are created at `0600`, and reads use `O_NOFOLLOW` plus a regular-file check.
File storage is content-addressed, atomic, and rejects path traversal and
reparse-point reads; downloads re-hash bytes before returning them. Local OS users
who are not the service account therefore cannot read artifacts through the filesystem
and must use the authenticated download API.

Example provisioning uses a dedicated persistent volume or sized filesystem mounted
at the artifact root, with a device capacity and inode limit at or below the
configured maxima:

```bash
export Pkgsrv__Artifacts__QuotaBytes=1073741824
export Pkgsrv__Artifacts__QuotaInodes=1000000
```

```conf
# /etc/fstab (illustrative; an LVM/loop-backed dedicated filesystem is required)
/dev/pkgsrv/artifacts /var/lib/pkgsrv/artifacts ext4 defaults,nodev,nosuid,noexec 0 2
```

Or an equivalent systemd mount unit:

```ini
# /etc/systemd/system/var-lib-pkgsrv-artifacts.mount
[Unit]
Description=Dedicated pkgsrv artifact filesystem

[Mount]
What=/dev/pkgsrv/artifacts
Where=/var/lib/pkgsrv/artifacts
Type=ext4
Options=nodev,nosuid,noexec
```

```conf
# /etc/tmpfiles.d/pkgsrv-artifacts.conf
d /var/lib/pkgsrv/artifacts 0700 pkgsrv pkgsrv -
```

The artifact root must itself be an exact mountpoint on a separate persistent
filesystem/device; `tmpfs` and `ramfs` are rejected. Mountinfo must show the
filesystem root as `/` (not a bind-mounted subdirectory) and a device distinct from
the mount's immediate parent; bind mounts of a larger shared filesystem are rejected.
It must be pre-provisioned as a service-owned `0700` directory with no symlink
components. When builds are enabled, startup verifies that the filesystem's total
bytes and inodes are within `Pkgsrv:Artifacts:QuotaBytes` and
`Pkgsrv:Artifacts:QuotaInodes`, and that currently available bytes are sufficient
for `Pkgsrv:Build:MaxArtifactBytes` plus metadata overhead before any Git checkout,
bubblewrap run, or publication. Failure fails host startup.

`Pkgsrv:Artifacts:QuotaBytes` and `Pkgsrv:Artifacts:QuotaInodes` are upper-bound
validation ceilings only; they are never enforced as a runtime quota. The actual
capacity boundary must be the operator-provisioned separate filesystem/device.
Configuring a large ceiling cannot make a shared or bind-mounted larger filesystem
acceptable. A larger shared filesystem with a project quota is not currently
verifiable by this validator and is rejected; only a dedicated device whose
`statvfs` totals fit the configured ceilings is accepted. The process-local
in-memory artifact store remains available only when builds are disabled (for
example, local tests). The build workspace root is
stricter still: it must be a dedicated capped tmpfs and is never created by the
application.

Signing is enabled only when an RSA private key file is configured:

```bash
export Pkgsrv__Signing__PrivateKeyPath=/etc/pkgsrv/signing-private.pem
export Pkgsrv__Signing__PublicKeyPath=/etc/pkgsrv/signing-public.pem
```

The private key is read from the configured file path only, never logged, and is
used to produce an RSA-SHA256 PKCS#1 signature over the artifact SHA-256 digest.
On Linux the private key must be a regular non-symlink file owned by the service
account with mode `0400` or `0600` and no group/other bits; RSA keys smaller than
2048 bits are rejected. Public keys are not secret but must also be regular
non-symlink files.
If private-key configuration is absent, `PendingPackageSigner` and the refusing
publisher remain active; no fake signature is generated. Publishing and downloading
require a publishable manifest signature that verifies against the configured
public key, a matching artifact content hash, and verified stored bytes.

Publication flow:

1. `POST /packages/{name}/{version}/publish` requires the `publish` permission.
2. The package must be in `BuildRequested` state and have a stored artifact.
3. A pending manifest is signed with the configured RSA key; the signature is
   verified before persistence.
4. The publisher verifies manifest signature, artifact hash, signature identity,
   and content-addressed bytes.
5. Only after publisher success is `Published` written with a conditional update
   and `PublishedAt`; a failed publisher or failed database write leaves the package
   unpublished.
6. Publication is refused if any check fails.

Download flow:

1. `GET /packages/{name}/{version}/download` requires the `read` permission.
2. The package must be durably `Published` with a non-null `PublishedAt`.
3. The manifest must be signed and verifiable, and stored bytes are re-hashed and
   matched to the manifest/package key.
4. Only then are bytes returned as `application/octet-stream`.

No request accepts an artifact path, executable, shell command, or build output
path.

The schema is owned by EF Core. `PostgresPackageDbContext`, the model snapshot, and
the migration classes under `Persistence/Postgres/Migrations` are canonical, and
`__EFMigrationsHistory` is the only authoritative migration state. The migration
chain is:

- `20261125182608_InitialCreate` — legacy-compatible baseline that creates the
  package table on a fresh database and adopts an existing old table without
  dropping or truncating data.
- `20261125182644_AddVersionOrderColumns` — adds and backfills the numeric
  major/minor/patch, pre-release, and build-metadata columns and their ordering
  index.
- `20261126095757_AddPublicationTimestamp` — adds the nullable `PublishedAt`
  publication marker non-destructively.
- `20261126210108_AddPackageOutbox` — creates the `package_outbox` table with the
  idempotency-key primary key, package key, event type/payload, occurrence and
  availability timestamps, claim lease, attempt count, status, and last-error
  fields, plus package and status/availability indexes.
- `20261127055233_AddSemVerPrecedenceKey` — adds and backfills the pre-release
  precedence sort key and replaces the ordering index to use the same SemVer key
  as the in-memory comparer.

With `InitializeSchema=true`, startup calls `Database.MigrateAsync()` before
listening. With `InitializeSchema=false`, startup never applies migrations; it
checks for pending migrations and fails clearly when migrations are pending or the
database/migration history is unavailable. PostgreSQL mode never silently falls
back to in-memory storage.

An existing old custom-v1 schema is upgraded by the same EF migration chain: the
baseline detects the existing `packages` table, adds missing timeout/pending-event
columns and backfills timeout ticks, then the version-order migration adds and
backfills version columns, the publication migration adds the nullable
`PublishedAt` marker, and the SemVer precedence migration adds and backfills the
pre-release sort key. Existing rows and publication state are preserved. The old
`schema_versions` table may remain in place but is not used or treated as
authoritative.

Create a migration and apply it with the standard EF commands:

```bash
dotnet ef migrations add <MigrationName> \
  --project pkgsrv/src/Pkgsrv.Infrastructure \
  --startup-project pkgsrv/src/Pkgsrv.Infrastructure \
  --output-dir Persistence/Postgres/Migrations

dotnet ef database update \
  --project pkgsrv/src/Pkgsrv.Infrastructure \
  --startup-project pkgsrv/src/Pkgsrv.Infrastructure
```

The design-time factory reads `PKGSRV_DESIGN_POSTGRES_CONNECTION` when generating or
applying migrations.

Opt-in integration tests run only when `PKGSRV_TEST_POSTGRES_CONNECTION` is set;
otherwise the PostgreSQL tests are reported as skipped. They cover fresh creation,
old-v1 upgrade/data preservation through the EF migration chain, atomic
package+outbox commit/rollback, duplicate idempotency-key deduplication, and
claim/retry/delivered replay transitions.

## Issue #175 acceptance evidence

- Application port: `PackageCacheInvalidation` carries an all, list, or optional
  package-key scope without Presentation dependencies. Authorization decisions
  and errors remain uncached, and every cache read still authorizes the actor
  before checking the cache.
- Write path: register, build, and publish invalidate the local generation
  immediately after the durable mutation commits. A pending manifest signature
  update invalidates before the external publisher runs, so a publisher refusal
  cannot leave another instance serving stale pending-signature JSON. Failed
  writes and already-published retries publish no unchanged-state event. The
  notifier performs local invalidation first and contains publication failures
  after that point, so an unavailable listener cannot turn a successful package
  write into a failure.
- PostgreSQL: fixed quoted `LISTEN` channel, fixed parameterized
  `pg_notify(@channel, @payload)` SQL, JSON payload limited to
  scope/name/version, background listener with bounded exponential backoff and
  clean cancellation, and no startup database connection in InMemory mode.
- InMemory: process-local invalidation only; the absolute TTL remains the
  bounded staleness fallback, and no external cache dependency is added.
- Tests: two cache instances invalidate through an injectable bus seam; local
  register/build/publish invalidation; no event on failed writes; duplicate,
  concurrent, and out-of-order events are harmless; an unavailable publisher
  does not break the write path; a deterministic delayed publisher proves
  signature notification completes before external publication and publication
  notification follows it; Postgres payload/channel unit tests; and an opt-in
  live PostgreSQL integration test skipped unless
  `PKGSRV_TEST_POSTGRES_CONNECTION` is set.
- Package domain events now use the durable transactional outbox from
  [#176](https://github.com/arlirad/astia/issues/176); cache invalidation remains a
  separate best-effort notification channel with the absolute TTL fallback. No
  deployment configuration changes are part of #176.

## Issue #176 acceptance evidence

- Transactional write path: PostgreSQL `IPackageEventOutbox` is implemented by the
  repository. Register and build handlers use it when composed, so the package
  insert/state update and its supported outbox rows commit or roll back together.
  In-memory composition registers no outbox and keeps the synchronous
  `InMemoryDomainEventPublisher` path.
- Schema and idempotency: `package_outbox` stores the idempotency key as its primary
  key plus package key, event type, deterministic JSON payload, occurrence and
  availability timestamps, claim lease, attempt count, status, and last error. Only
  `PackageRegistered` and `PackageBuildRequested` are serialized; payloads contain no
  authorization tokens or connection secrets. `PendingEventsJson` compatibility
  data remains written and is not silently dropped.
- Dispatch semantics: a PostgreSQL-only hosted dispatcher polls bounded batches,
  claims rows with `FOR UPDATE SKIP LOCKED` plus a lease, publishes through an
  injected adapter that exposes the idempotency key, and marks delivered only after
  publication succeeds. Failures retain the row with bounded exponential backoff and
  a maximum attempt count; exhausted rows remain `Failed`. Lease expiry and restart
  replay pending rows. Dispatcher or publication failures cannot roll back a
  committed package write, and the hosted service does not block startup.
- Tests: DB-free coverage asserts serializer determinism/round-trip and unsupported
  event rejection, retry/backoff bounds, dispatcher success/failure/exhaustion,
  mark-delivered ordering, restart replay, handler atomic commit/rollback behavior,
  no synchronous publish in outbox mode, and InMemory/PostgreSQL composition
  differences. The opt-in live PostgreSQL suite verifies atomic package+outbox
  commit, rollback on outbox write failure, duplicate idempotency-key deduplication,
  and claim/retry/delivered replay; it is skipped unless
  `PKGSRV_TEST_POSTGRES_CONNECTION` is set.
- Delivery is at-least-once and consumers must deduplicate on the idempotency key.
  Live PostgreSQL outbox conformance remains part of the opt-in
  [#174](https://github.com/arlirad/astia/issues/174) harness. No deployment
  configuration changes are part of this issue.
- Residuals: the default composition still binds the in-memory event publisher, so
  external delivery requires a deployment-supplied `IDomainEventPublisher`/consumer
  adapter. The migration retains legacy `PendingEventsJson` rows but does not
  auto-enqueue them into `package_outbox`; a deployment with pre-existing
  undelivered pending events needs an explicit backfill/replay.

## Issue #179 acceptance evidence

- Build execution: opt-in, fixed operator profiles, bubblewrap Namespace only,
  offline-only profiles with no `--share-net`, client network policy unable to
  override profiles, timeout/process-tree kill, private pre-provisioned workspace
  root, required dedicated capped tmpfs byte/inode quota, symlink/reparse checks,
  regular-file-only output reads with a deadline, hash-before-store, safe cleanup,
  workspace-backed private `/tmp` with no unbounded tmpfs, bounded artifact size,
  and no fallback.
- Source egress: exact host:port and exact IP admission rules, HTTPS-only,
  redirects disabled, isolated Git configuration, and a required operator egress
  proxy before checkout.
- Signing/publication: RSA key-file signing/verification, refusing defaults when
  keys are absent, publisher validation, durable `Published`/`PublishedAt` marker,
  conditional publication CAS, and download gated on durable publication.
- Storage: content-addressed filesystem artifacts when configured with a
  pre-provisioned `0700` service-owned root on an exact durable mountpoint, verified
  artifact byte/inode caps and current free capacity, `0700` package directories,
  `0600` files, no-follow reads, and in-memory safe fallback only when builds are
  disabled; schema v3 for publication.
- Tests: sandbox refusal/success/timeout, FIFO/no-hang, hash-mismatch-before-store,
  workspace quota guards, artifact capacity admission, restrictive-workspace
  cleanup, offline profile network rejection, real conditional bubblewrap smoke,
  artifact mode/symlink/path checks, RSA refusal/success, publish/download
  authorization, publication failure/retry and download denial, rehydration, schema
  migration, source port/IP allowlist rules, and missing-egress-proxy failure.
- Residuals are listed below; none are claimed as implemented.

## Issue #180 acceptance evidence

- Liveness: `GET /healthz` remains a `200` `{"status":"ok"}` process marker and does
  not call the database.
- Readiness contract: `GET /readyz` returns `application/json` with `status`,
  `storageProvider`, `storageDurable`, `schemaVersion`, and `postgresAvailable`;
  in-memory mode is ready but non-durable.
- PostgreSQL readiness: a bounded probe opens a fresh `DbContext`, verifies
  connectivity, reads pending/applied EF Core migrations, and requires the applied
  migration set to match the migrations known to this build. Pending, unknown,
  unavailable, timed-out, or failed state returns `503` with the same safe payload.
- Bounded time: `Pkgsrv:Readiness:PostgresProbeTimeoutMs` defaults to `2000` and is
  capped at `10000`; the probe uses a linked cancellation token and holds no
  long-lived connection.
- Failure semantics: a missing or throwing readiness probe returns `503` JSON, not
  HTML or `500`. Startup migration behavior and in-memory fallback are unchanged.
- Redaction: responses contain only the five contract fields; connection strings,
  hostnames, credentials, filesystem paths, mount sources, and exception messages
  are never serialized.
- Tests: in-memory readiness, PostgreSQL success, pending/unknown migration state,
  unavailable database, bounded timeout, external cancellation, missing probe,
  throwing probe, payload redaction, and HTTP status/JSON for `/healthz` and
  `/readyz`, all without a live database.
- Residual: CI exercises the probe with fakes; live PostgreSQL connectivity and
  migration-state probing remain covered by the opt-in integration tests tracked in
  [#174](https://github.com/arlirad/astia/issues/174).

## Issue #177 acceptance evidence

- Search contract: search supports package `name` contains and source commit prefix,
  both case-insensitive. Bounded SQL filtering is preserved, but neither field is
  claimed as indexed unless verified in the current EF migration/schema. Provenance
  and other manifest metadata remain returned data, not search fields.
- Alignment: PostgreSQL `BuildSearchQuery` and the in-memory repository apply the
  same two-field contract; provenance-only terms match in neither implementation.
- Semantics preserved: source commit matching stays prefix-only, `%` and `_` remain
  escaped/literal, deterministic version ordering is unchanged, and the 1000-entry
  result cap still applies before PostgreSQL materialization.
- No schema change: no `pg_trgm` extension, trigram index, or migration is added.
  Broad provenance containment search remains a tracked residual until a reviewed
  index strategy exists.
- Tests: PostgreSQL SQL-shape assertions exclude provenance and retain escaping,
  ordering, and limiting; in-memory tests cover name contains, source commit prefix,
  provenance exclusion, and wildcard literals; the opt-in PostgreSQL integration
  test exercises the same behavior when its database connection is configured.

## Issue #197 acceptance evidence

- Precedence: numeric identifiers compare numerically, alphanumeric identifiers
  lexically in ASCII order, numeric identifiers sort below alphanumeric ones,
  shorter identifier sets sort first when prefixes match, and releases sort after
  every pre-release. Build metadata does not affect precedence; canonical version
  text is the deterministic final tiebreak.
- Parity: `PackageVersionOrder.CreatePreReleaseSortKey` is the single encoder used
  by the in-memory comparer and persisted by `PostgresPackageMapper`;
  `BuildSearchQuery` orders by the same key under the `C` collation, so both stores
  return the same order. The migration backfills existing rows with that encoding.
- Search: canonical version text, including pre-release and build metadata, is a
  case-insensitive contains field in both stores. Name contains and source commit
  prefix behavior is unchanged, and `%`/`_` remain literal. No `pg_trgm` extension
  or trigram index is added; version search is not claimed as indexed, consistent
  with the reviewed [#177](https://github.com/arlirad/astia/issues/177) strategy.
- Invalid input: numeric pre-release identifiers with leading zeroes are outside
  SemVer and are encoded as alphanumeric text, giving deterministic ordering
  without changing registration validation.
- Tests: the SemVer specification example, rc.2 vs rc.10, numeric vs alphanumeric
  identifiers, prefix and shorter-set ordering, release vs pre-release,
  build-metadata ties, leading-zero handling, sort-key/comparer parity, in-memory
  and SQL-shape search coverage, migration/model assertions, and opt-in live
  PostgreSQL ordering-parity and version-search tests that skip without
  `PKGSRV_TEST_POSTGRES_CONNECTION`.

## JSON read cache

Successful `GET /packages` and `GET /packages/{name}/{version}` responses are
served from a bounded in-process JSON cache. Every cache request authorizes the
actor before checking the cache, so a cached response is never returned without a
fresh authorization decision. Authorization decisions and errors are not cached.
The cache key includes the caller identity, the list query, and the package key.
Actor identity fields and roles are length-prefixed before being combined, so
delimiter-bearing header values cannot collide. Unfiltered list results use an
explicit `all` key segment; filtered results use `search:<query>`, so a literal `*`
search cannot collide with the unfiltered list. JSON is generated only on a cache
miss, then returned with `application/json`.

| Setting | Default | Behavior |
| --- | --- | --- |
| `Pkgsrv__Cache__TtlSeconds` | `5` | Absolute entry lifetime |
| `Pkgsrv__Cache__SizeLimit` | `1024` | Maximum cached entries |

Successful registration invalidates the cache immediately after the package insert
commits, before domain-event publication or acknowledgement. Build requests
invalidate immediately after the state transition commits, before publication,
acknowledgement, or sandbox execution. Publication invalidates immediately after
a pending manifest's signature update commits, before the external publisher is
called, and again after the published state commits. A later workflow or
publisher failure therefore cannot leave stale pending-signature or unpublished
JSON behind. A load captures the generation before it
starts; if a mutation invalidates the cache while that load is in flight, the stale
result is not stored for future reads. Entries also expire by absolute TTL, and the
`MemoryCache` size limit prevents unbounded growth.

Invalidation is shared between PostgreSQL-backed instances through a
`LISTEN`/`NOTIFY` channel named `pkgsrv_package_cache_invalidation`. Successful
registration, build requests, and publication notify only after the durable write
commits; a failed write publishes nothing. The payload contains only the
invalidation scope and, for a package-scoped event, the package name and version:
no tokens, authorization data, or connection strings are sent. Each instance
invalidates its own cache first, then awaits notification publication. This keeps
the signature-mutation event ahead of the external publisher and the
publication-state event after `MarkPublished`. A notification or listener
failure is logged and never changes the result of a successful package write.
The listener reconnects with bounded exponential backoff and stops cleanly with
host shutdown.

The in-memory provider has no cross-process channel, so invalidation is
process-local. Notifications are best-effort and can be lost during a transient
PostgreSQL outage. The bounded absolute TTL still caps how long an instance can
serve stale data. Cache invalidation notifications remain best-effort and are not
part of the package-event transactional outbox added in
[#176](https://github.com/arlirad/astia/issues/176).

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
| `GET` | `/healthz` | `200` liveness marker; does not touch storage; no bearer token required |
| `GET` | `/readyz` | `200` when storage is ready or `503` when not; safe JSON payload; no bearer token required |
| `POST` | `/packages` | `201` registered package; `400`/`401`/`403`/`409`/`422`/`503` on failure |
| `GET` | `/packages` | `200` package summaries; `401`/`403`/`503` on authorization failure |
| `GET` | `/packages/{name}/{version}` | `200` package details; `401`/`403`/`404`/`503` |
| `POST` | `/packages/{name}/{version}/build` | `202` with `NotExecuted`/build result; `401`/`403`/`404`/`409`/`503` |
| `POST` | `/packages/{name}/{version}/publish` | `200` published signature/hash; `401`/`403`/`404`/`409`/`503` |
| `GET` | `/packages/{name}/{version}/download` | `200` verified artifact bytes; `401`/`403`/`404`/`409`/`503` |

The `GET /packages` route currently returns the full bounded list and does not expose
a search query parameter; the application index supports the restricted
name/version/source-commit search described above.

Every route except `/healthz` and `/readyz` requires `Authorization: Bearer <Irva access token>`.
There are no pkgsrv login/register/session/refresh/proxy-auth endpoints. Error
responses use RFC 7807 problem details with a stable `code` extension.

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
export Pkgsrv__Sources__AllowedHosts__2='git.internal.example:8443'
export Pkgsrv__Sources__AllowedHosts__3=203.0.113.10
export Pkgsrv__Sources__EgressProxy=http://127.0.0.1:3128
```

With no configured hosts, registration is rejected by the allowlist. HTTPS source
URLs on a non-443 port require an exact `host:port` entry. IP-literal hosts require
an exact entry; wildcard entries never match IP literals. These are admission
checks only: source checkout still requires an operator-configured egress proxy
that enforces the host/IP policy at connection time. The host allowlist alone does
not prevent DNS rebinding to a private or internal address.

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

- Sandboxed execution is implemented for bubblewrap `Namespace` isolation only.
  `Container` and `VirtualMachine` return `NotExecuted`; cgroup memory/CPU limits
  are not applied. Source checkout uses host networking before sandbox startup and
  therefore requires an operator-provided egress proxy; the source-host allowlist
  alone does not prevent DNS rebinding.
- Workspace disk usage is bounded only by the operator-provisioned dedicated tmpfs
  mount with both `size=` and `nr_inodes=` caps checked at runtime, plus exact
  mountpoint/root/distinct-device verification. There is no per-build filesystem
  quota, project quota, or disk-I/O controller; a bind mount, non-mount, `ramfs`, or
  a mount without either cap fails closed.
- Artifact storage is durable only when `Pkgsrv:Artifacts:Root` is configured and
  pre-provisioned as a private `0700` service-owned exact mountpoint on a separate
  persistent filesystem/device, with mountinfo root `/` and a device distinct from
  its parent. `QuotaBytes`/`QuotaInodes` are validation ceilings, not quotas: the
  operator-provisioned separate filesystem/device is the actual capacity bound.
  Builds fail to start without a verifiable dedicated mount,
  ceilings, or sufficient currently available capacity; the in-memory artifact store
  is only for builds-disabled/local-test operation. The application does not create
  the artifact root or mutate mount configuration.
- PostgreSQL durability depends on a configured database and a provisioned schema;
  in-memory mode is not durable.
- The transactional outbox dispatcher is PostgreSQL-only. Delivery is
  at-least-once, so consumers must deduplicate on the exposed idempotency key;
  exhausted rows remain `Failed` until an operator replays them. Live PostgreSQL
  outbox integration remains opt-in and is skipped without
  `PKGSRV_TEST_POSTGRES_CONNECTION`.
- Versioned schema DDL is owned by EF Core migrations under
  `Persistence/Postgres/Migrations`; `__EFMigrationsHistory` is authoritative.
  `InitializeSchema=true` applies pending migrations, while `false` fails clearly
  when migrations are pending.
- Version ordering implements full SemVer 2.0 identifier precedence, and search
  includes canonical version text.
- Search supports the bounded fields package `name` contains, canonical `version`
  contains, and source commit prefix. Bounded SQL filtering is preserved, but the
  fields are not claimed as indexed unless verified in the current EF
  migration/schema. Provenance is returned metadata but is not searchable until a
  reviewed trigram/index strategy exists
  ([#177](https://github.com/arlirad/astia/issues/177)).
- The real Irva.Client project is optional and external. Default builds compile the
  fail-closed deny adapter; operators must build with `IrvaClientProject` and set
  `Pkgsrv:Irva:*` explicitly. Without both, package requests are denied.
- No project reference is made to `/home/tymk/Projects/_prg/servers/ervan` or
  `/home/tymk/Projects/_prg/servers/irva`; the hosting composition must supply the
  external path and share Irva's application registration.
- Pkgsrv is only a resource server. It does not provide login, registration,
  session, refresh-token, password, or auth-proxy endpoints; Irva remains the
  identity provider.
- Build requests without an enabled runtime/allowlisted profile return
  `NotExecuted`; this is deliberate.
- The application deliberately does not create the Unix socket runtime directory.

### Residual #178 limitations

- The default solution build does not compile the real Irva.Client adapter, so CI
  verifies the boundary, fakes, and fail-closed composition rather than the external
  client project.
- The live Irva integration test is opt-in and requires an external checkout plus
  `PKGSRV_TEST_IRVA_*` environment variables; it is skipped in normal CI.
- Irva permissions are checked through `HasPermission` after `VerifyAsync`; pkgsrv
  does not implement local permission caching, roles, account management, or a token
  revocation feed beyond what Irva returns per verification.
- Cross-origin browser deployment still needs a reverse proxy or explicit CORS
  policy; pkgsrv itself binds a Unix socket or loopback TCP by default.

## Tracked residuals

- Issue #179 residuals: no cgroup CPU/memory limits; workspace byte/inode
  enforcement depends on an operator-provisioned dedicated tmpfs mount with both
  `size=` and `nr_inodes=` plus exact root/distinct-device mount identity; artifact
  capacity enforcement depends on a separate persistent mount/device; a project
  quota on a larger filesystem is not verifiable yet and fails closed; configured
  Quota values are only validation ceilings; source
  egress enforcement depends on an operator-provided allowlisting proxy; no
  network-enabled build profile is executable until an enforceable egress-proxy
  namespace policy exists; no per-build disk-I/O controller.
- [#174](https://github.com/arlirad/astia/issues/174) - live PostgreSQL integration
  and migration conformance tests.
- [#176](https://github.com/arlirad/astia/issues/176) - PostgreSQL transactional
  outbox dispatch/retry is implemented; live database outbox conformance remains
  opt-in and is covered with the [#174](https://github.com/arlirad/astia/issues/174)
  live PostgreSQL test harness when `PKGSRV_TEST_POSTGRES_CONNECTION` is set.
- [#177](https://github.com/arlirad/astia/issues/177) - search supports name
  contains and source-commit prefix with bounded SQL filtering; neither field is
  claimed as indexed unless verified in the current EF migration/schema; provenance
  remains returned metadata but is not searchable until a reviewed trigram/index
  strategy exists.
- [#178](https://github.com/arlirad/astia/issues/178) - external Irva.Client build
  wiring and live integration test remain opt-in; residual limitations are listed
  above.
- [#180](https://github.com/arlirad/astia/issues/180) - readiness now reports
  storage provider/schema version and bounded PostgreSQL availability; live
  PostgreSQL readiness conformance remains part of the opt-in #174 integration
  coverage.
- [#197](https://github.com/arlirad/astia/issues/197) - full SemVer precedence and
  version-aware search.
- Container/VM isolation adapters and cgroup CPU/memory resource limits.
