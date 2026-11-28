# Vendored Irva source

- Upstream: `ssh://tymk@tymkserver/~/git/irva`
- Commit: `b205b6486607bdde44893bc5d47b39b4ee0876aa`
  (`Skip HTTPS validation when using Unix socket`)
- Vendored on: 2026-11-28
- Contents: 36 tracked files (33 `.cs`, 3 `.csproj`) from
  `src/Irva.Client`, `src/Irva.Dtos`, and `src/Irva.Core`
- License: MIT, user-owned Irva source (see `LICENSE`)

Pkgsrv is built from a container context that contains only `pkgsrv/`.
The upstream checkout is not part of that context, so the three projects needed
by `IrvaClientAdapterFactory.Real.cs` are vendored verbatim. `Pkgsrv.Infrastructure`
references `src/Irva.Client/Irva.Client.csproj` through the `IrvaClientProject`
MSBuild property (set in `Dockerfile`).

Only git-tracked source is present. `bin/`, `obj/`, test projects, local
configuration, tokens, keys, and other secrets are deliberately excluded.
NuGet packages are restored from the configured feed as usual.

Update procedure:

1. Review the upstream commit and confirm the pinned SHA.
2. Run:
   `git -C /home/tymk/Projects/_prg/servers/irva archive <sha> src/Irva.Client src/Irva.Dtos src/Irva.Core | tar -x -C pkgsrv/vendor/irva`
3. Regenerate `VENDOR.sha256`:
   `cd pkgsrv/vendor/irva && find src -type f -print0 | sort -z | xargs -0 sha256sum > VENDOR.sha256`
4. Build with the vendored project:
   `dotnet build pkgsrv/src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj -p:IrvaClientProject=$PWD/pkgsrv/vendor/irva/src/Irva.Client/Irva.Client.csproj`
