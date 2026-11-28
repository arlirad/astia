# Pkgsrv deployment

Pkgsrv is deployed as a rootless Podman Quadlet unit. It listens only on a Unix
domain socket, `unix:/run/pkgsrv/pkgsrv.sock` inside the container, which is the
host socket `/sock/pkgsrv.sock`. Arbiter publishes `pkg.astia.nodivia.pl` and
proxies that host binding to `unix:///sock/pkgsrv.sock`.

The container and deploy target are defined by:

- `pkgsrv/Dockerfile` and `pkgsrv/.dockerignore`
- `pkgsrv/entrypoint.sh`
- `pkgsrv/pkgsrv.container`
- `pkgsrv/pkgsrv.env.example`
- `deployables.yaml` targets `pkgsrv` and `pkgsrv-site`
- `arbiter-sites/pkgsrv.yaml`

The build uses the vendored `Irva.Client`/`Irva.Dtos`/`Irva.Core` sources in
`vendor/irva` (pin: `b205b6486607bdde44893bc5d47b39b4ee0876aa`, see
`vendor/irva/VENDOR.md` and `vendor/irva/LICENSE`). No external checkout is
needed in the build context.

## Host prerequisites

The deployment host must be provisioned before the first deploy. Nothing in this
repository performs these privileged steps.

1. **Rootless Podman with crun and `keep-id` UID/GID mapping.** The image runs as
   the .NET `APP_UID` (1654) and the Quadlet maps that container UID/GID to the
   host deploy user with `UserNS=keep-id:uid=1654,gid=1654`. Podman 4.4 or newer
   is required. Verify:

   ```sh
   podman --version
   podman info --format '{{.Host.OCIRuntime.Name}}'
   ```

   Rootless Podman must use crun. Without `keep-id`, container UID 1654 maps to a
   subuid and cannot write the host-owned socket, artifact, or key paths.

2. **Artifact filesystem.** `/var/lib/pkgsrv/artifacts` must be a dedicated
   persistent filesystem or volume, not a bind-mounted subdirectory of a shared
   filesystem and not `tmpfs`/`ramfs`. Pkgsrv expects an existing `0700`
   directory owned by the service account (the host deploy user, because of the
   `keep-id` mapping) and rejects symlink components.

   Illustrative provisioning (adapt to the host; this does not create or
   partition storage):

   ```sh
   sudo install -d -m 0700 -o deploy -g deploy /var/lib/pkgsrv/artifacts
   ```

   If builds are enabled later, `Pkgsrv__Artifacts__QuotaBytes` and
   `Pkgsrv__Artifacts__QuotaInodes` must also be set and the filesystem's total
   capacity must fit those ceilings. Builds are disabled in this deployment.

3. **PostgreSQL.** Create the database and role out of band. Pkgsrv applies the
   EF Core migration chain at startup because
   `Pkgsrv__Database__InitializeSchema=true`. Keep the connection string only in
   `/home/deploy/.config/pkgsrv.env` (mode `0600`).

   The container uses host networking, so a database listening on host loopback
   is reachable, as is a remote database. The provider is
   `Pkgsrv__Database__Provider=Postgres` and is set in the Quadlet.

4. **Irva registration and application token.** Register pkgsrv as an Irva
   application with the flat permission tree:

   ```yaml
   permissions:
     read:
     build:
     publish:
   ```

   Grant those permissions to the accounts or roles allowed to use pkgsrv. Put
   the Irva base URL and pkgsrv application token only in
   `/home/deploy/.config/pkgsrv.env` as `Pkgsrv__Irva__BaseUrl` and
   `Pkgsrv__Irva__AppToken`. The Quadlet sets `Pkgsrv__Irva__Enabled=true` and
   `Pkgsrv__Irva__Required=true`, so startup fails rather than serving without
   working authorization.

   The container uses host networking, so the stock Irva loopback listener is
   reachable. If Irva is moved to a Unix socket, set
   `Pkgsrv__Irva__SocketAddress` and mount that socket path; do not put tokens in
   the Quadlet, image, or repository.

5. **Signing keys.** Place the RSA private and public keys under `/etc/pkgsrv/`:

   ```sh
   sudo install -d -m 0500 -o deploy -g deploy /etc/pkgsrv
   # Create the key pair with the operator's approved key-management procedure.
   sudo install -m 0400 -o deploy -g deploy signing-private.pem /etc/pkgsrv/signing-private.pem
   sudo install -m 0400 -o deploy -g deploy signing-public.pem  /etc/pkgsrv/signing-public.pem
   ```

   The private key must be a regular non-symlink file owned by the service
   account with mode `0400` or `0600`; RSA keys smaller than 2048 bits are
   rejected. `/etc/pkgsrv` is mounted read-only into the container. Never commit
   key material.

6. **Socket directory and Arbiter access.** Create the shared host socket
   directory:

   ```sh
   sudo install -d -m 0770 -o deploy -g deploy /sock
   sudo setfacl -m u:arbiter:rwx /sock
   sudo setfacl -m d:u:arbiter:rwx /sock
   ```

   The container creates `/sock/pkgsrv.sock` as the mapped deploy user with a
   group/other-masked socket mode (`UMask=0007`). Arbiter runs as a separate
   service account and needs traverse plus connect access; the ACL above grants
   it. The default ACL lets the socket pick up the same access when it is
   recreated. Do not make `/sock` world-writable.

   The entrypoint removes a stale socket only when the path is a socket and is
   owned by the container UID; it refuses to delete files, directories, symlinks,
   or sockets owned by another account.

7. **Environment file.** Copy `pkgsrv.env.example` to
   `/home/deploy/.config/pkgsrv.env`, fill the three required values, set mode
   `0600`, and keep it owned by the deploy user. The Quadlet references that path
   through `EnvironmentFile=`; the repository contains key names only.

## Deploy

From the canonical deploy repository, after review:

```sh
python3 deploy.py --list
python3 deploy.py pkgsrv-site
```

`pkgsrv-site` requires `pkgsrv`, so the deploy tool rebuilds and restarts the
container first, then publishes `arbiter-sites/pkgsrv.yaml` and removes the old
static `pkg.astia.yaml` site file if the host still has one. The old static
package repository mapping is not part of this branch.

The deploy tool uploads only `pkgsrv/` as the build context, builds
`localhost/pkgsrv:latest` on the host, installs
`~/.config/containers/systemd/pkgsrv.container`, and restarts the user unit.

## Validation

Local checks, no SSH, Podman, or deployment:

```sh
python3 pkgsrv/tools/verify_deploy_assets.py
python3 -m py_compile deploy.py
python3 deploy.py --list
python3 -m unittest discover -s tests -v
```

Vendor integrity:

```sh
cd pkgsrv/vendor/irva && sha256sum -c VENDOR.sha256
```

Build the real Irva adapter against the verdict pin without a container:

```sh
dotnet build pkgsrv/src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj \
  -p:IrvaClientProject="$PWD/pkgsrv/vendor/irva/src/Irva.Client/Irva.Client.csproj"
```

## Rollback

1. **Image.** The deploy target always builds `localhost/pkgsrv:latest`, so tag
   the known-good image before a deploy:

   ```sh
   podman tag localhost/pkgsrv:latest localhost/pkgsrv:rollback-$(date +%Y%m%d%H%M%S)
   ```

   To roll back, retag the known-good image as `localhost/pkgsrv:latest` and run
   `systemctl --user restart pkgsrv.service`.

2. **Unit and site config.** Keep a copy of the previous
   `~/.config/containers/systemd/pkgsrv.container` and
   `/etc/arbiter/sites/pkgsrv.yaml`. Restore them and run
   `systemctl --user daemon-reload && systemctl --user restart pkgsrv.service`.
   Re-run the approved site publication for Arbiter configuration.

3. **Database and artifacts.** EF Core migrations are forward-only. Take a
   PostgreSQL backup before the first deployment and before any later schema
   change. Artifacts are content-addressed and durable; do not delete the
   artifact filesystem during an application rollback.

4. **Old static mapping.** This branch removes the `pkg.astia` static target and
   publishes `pkg.astia.nodivia.pl` through Arbiter to pkgsrv. Rolling back to
   the old static package repository is a separate, reviewed deploy change; do
   not hand-edit remote Arbiter files.

## Remaining host blockers

- Build execution (`Pkgsrv__Build__Enabled`) stays `false`. Enabling it requires
  the dedicated capped tmpfs workspace, bubblewrap, an egress proxy, source
  allowlists, and artifact quota ceilings described in `README.md`.
- TLS for `pkg.astia.nodivia.pl` is terminated by Arbiter/ACME, not by pkgsrv.
- No production deploy, SSH, push, or merge was performed for this change.
