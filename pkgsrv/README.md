# Pkgsrv

Pkgsrv is a framework-independent DDD skeleton for package registration, listing,
and build requests. It uses the deployed Ervan mediator setup exactly:
`Arlirad.Ervi.Mediator` 1.1.0, target `net10.0`, and `AddMediator(typeof(...))`.

The implementation is safe-by-default. It does not execute builds unless an operator
explicitly enables an allowlisted build profile and runtime, does not start
unconfined processes, does not publish unsigned artifacts, and does not claim real
Irva authorization. All dangerous capabilities are ports with non-executing or
denying adapters.

## Layers

| Project | Responsibility | Dependencies |
| --- | --- | --- |
| `src/Pkgsrv.Domain` | Package aggregate, value objects, manifest, build policy, domain events, invariants | BCL only |
| `src/Pkgsrv.Application` | Commands/queries, handlers, ports, DTOs, validation, authorization intent, permission names | Domain, Ervi mediator contracts |
| `src/Pkgsrv.Infrastructure` | In-memory and PostgreSQL package stores, source allowlist, safe build sandbox, artifact/signing adapters, pending Irva boundary | Application, Domain |
| `src/Pkgsrv.Presentation` | Minimal HTTP API, JSON read cache, request/response DTOs, actor context propagation, error mapping, composition root | Application, Infrastructure, Domain |
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

`Application.Interfaces.Security.IIrvaAuthorization` expresses the read, build,
and publish policy intent. Every application handler authorizes before doing work:

| Handler | Action | Permission name |
| --- | --- | --- |
| registration | `Publish` | `publish` |
| listing | `Read` | `read` |
| package details | `Read` | `read` |
| build request | `Build` | `build` |
| publication | `Publish` | `publish` |
| download | `Read` | `read` |

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
- Domain events are published from a stable snapshot. In PostgreSQL mode they are
  also persisted in the package row; both the in-memory aggregate and the durable
  pending-event representation are acknowledged only after publication succeeds.
  A publication or acknowledgement-persistence failure retains the pending event.
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
| `Pkgsrv__Database__InitializeSchema` | `false` | Runs the idempotent schema script at startup when true |

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
pre-release text with releases after pre-releases, build-metadata text, and
canonical version. Filtering, ordering, escaping, and the 1000-entry cap are applied
in SQL before materialization. Search treats `%` and `_` in user input literally by
escaping them, matching the in-memory `Contains`/`StartsWith` behavior.

Pending domain events are stored in the same row in `PendingEventsJson` and are
written atomically with registration/build state. Only the supported
`PackageRegistered` and `PackageBuildRequested` event kinds are serialized by
Infrastructure. On load, pending events are rehydrated into the aggregate without
raising new events. Acknowledgement removes only semantically published
descriptors and uses a compare-and-swap on the exact pending-events JSON with
bounded retries; newer pending events are preserved, and exhausted retries map to
the `409` package-conflict path. After successful publication, handlers clear the
persisted pending events and then the in-memory list. If publication or
acknowledgement persistence fails, the durable pending event remains.

This is an **at-least-once durable pending-event/outbox boundary**. Publishers must
be idempotent. Automatic outbox dispatch/retry is not implemented yet.

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

The schema lives in the default `public` schema as `packages`. It can be initialized
by setting `InitializeSchema=true`; startup executes an idempotent upgrade to the
current schema (equivalent to applying the versioned migrations in order) before
listening. The initializer never drops or truncates data. The versioned DDL is
available under `pkgsrv/db/migrations` and must be applied in order:

```bash
for migration in pkgsrv/db/migrations/*.sql; do
  psql "$PKGSRV_POSTGRES_CONNECTION" -f "$migration"
done
```

The required schema version is `3` (`0001_packages.sql`,
`0002_packages_version_order.sql`, and `0003_packages_publication.sql`). Startup
validates the exact version in `schema_versions` and the required package column
shape. A missing table, an old schema, a future version, or missing required
columns fails startup rather than silently claiming durability.
`InitializeSchema=false` therefore rejects an incompatible old schema before any
runtime query. The v3 migration adds the nullable `PublishedAt` marker
non-destructively. For production, prefer managed migrations or a reviewed DDL
deployment, and grant the runtime user only the required table privileges. The
application does not create databases, roles, or extensions, and does not silently
alter existing schemas.

An opt-in integration test runs only when
`PKGSRV_TEST_POSTGRES_CONNECTION` is set; otherwise it is a no-op.

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
acknowledgement, or sandbox execution. A later workflow failure therefore cannot
leave a stale detail/list entry behind. A load captures the generation before it
starts; if a mutation invalidates the cache while that load is in flight, the stale
result is not stored for future reads. Entries also expire by absolute TTL, and the
`MemoryCache` size limit prevents unbounded growth.

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
| `POST` | `/packages/{name}/{version}/build` | `202` with `NotExecuted`/build result, or `403`/`404`/`409` |
| `POST` | `/packages/{name}/{version}/publish` | `200` published signature/hash, or `403`/`404`/`409` |
| `GET` | `/packages/{name}/{version}/download` | `200` verified artifact bytes, or `403`/`404`/`409` |

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
- Automatic pending-event dispatch/retry is not implemented. The durable boundary
  is at-least-once, so publishers must be idempotent.
- Versioned SQL DDL is provided under `pkgsrv/db/migrations`; EF migrations are not
  used. `InitializeSchema=true` remains a skeleton convenience and is not an
  automatic production migration mechanism.
- Full SemVer identifier precedence and version-aware search are not implemented;
  list ordering is the deterministic contract above.
- Irva.Client authorization is pending and defaults to deny.
- No project reference is made to `/home/tymk/Projects/_prg/servers/ervan`; the
  permission and client adapter must be wired when hosting alongside Ervan.
- Build requests without an enabled runtime/allowlisted profile return
  `NotExecuted`; this is deliberate.
- The application deliberately does not create the Unix socket runtime directory.

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
- [#175](https://github.com/arlirad/astia/issues/175) - cross-instance read-model
  cache invalidation.
- [#176](https://github.com/arlirad/astia/issues/176) - transactional outbox
  dispatch/retry for package domain events.
- [#177](https://github.com/arlirad/astia/issues/177) - provenance contains-search
  index or restricted search fields.
- [#178](https://github.com/arlirad/astia/issues/178) - real Irva authorization and
  Ervan hosting boundary.
- [#180](https://github.com/arlirad/astia/issues/180) - storage mode and PostgreSQL
  availability in health/readiness.
- [#197](https://github.com/arlirad/astia/issues/197) - full SemVer precedence and
  version-aware search.
- Container/VM isolation adapters and cgroup CPU/memory resource limits.
