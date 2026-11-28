#!/usr/bin/env python3
"""Local structural checks for the pkgsrv container/deploy assets.

No network, Podman, SSH, or deployment is used.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "vendor" / "irva"
PIN = "b205b6486607bdde44893bc5d47b39b4ee0876aa"

errors: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def check_vendor() -> None:
    files = [
        path
        for path in VENDOR.rglob("*")
        if path.is_file() and not any(part in {"bin", "obj"} for part in path.parts)
    ]
    sources = [path for path in files if path.suffix in {".cs", ".csproj"}]
    check(len(sources) == 36, f"vendor source count: expected 36, found {len(sources)}")
    check(
        sum(path.suffix == ".cs" for path in sources) == 33,
        "vendor .cs count: expected 33",
    )
    check(
        sum(path.suffix == ".csproj" for path in sources) == 3,
        "vendor .csproj count: expected 3",
    )
    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "vendor/irva"],
        capture_output=True,
        text=True,
    )
    if tracked.returncode == 0:
        check(
            not re.search(r"/(bin|obj)/", tracked.stdout),
            "vendor tracks bin/ or obj/",
        )
    check(not any(path.suffix in {".dll", ".pdb", ".pem", ".key"} for path in files),
          "vendor contains build output or key material")

    manifest = read(VENDOR / "VENDOR.md")
    check(PIN in manifest, "VENDOR.md does not record the pinned commit")
    check("ssh://tymk@tymkserver/~/git/irva" in manifest,
          "VENDOR.md does not record the upstream URL")
    check("MIT" in read(VENDOR / "LICENSE"), "vendor LICENSE is not MIT")

    verify = subprocess.run(
        ["sha256sum", "-c", "VENDOR.sha256"],
        cwd=VENDOR,
        capture_output=True,
        text=True,
    )
    check(verify.returncode == 0, f"vendor hashes do not verify: {verify.stderr.strip()}")


def check_dockerfile() -> None:
    dockerfile = read(ROOT / "Dockerfile")
    required = [
        "mcr.microsoft.com/dotnet/sdk:10.0",
        "mcr.microsoft.com/dotnet/aspnet:10.0",
        "IRVA_CLIENT_PROJECT=/src/vendor/irva/src/Irva.Client/Irva.Client.csproj",
        "COPY vendor/irva/ vendor/irva/",
        'dotnet restore src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj',
        'dotnet publish src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj',
        "-p:IrvaClientProject",
        "USER ${APP_UID}",
        "ENTRYPOINT",
    ]
    for needle in required:
        check(needle in dockerfile, f"Dockerfile missing: {needle}")
    check("PublishPort" not in dockerfile, "Dockerfile should not publish a TCP port")
    check("ASPNETCORE_URLS" not in dockerfile, "Dockerfile must not force ASPNETCORE_URLS")

    ignore = read(ROOT / ".dockerignore")
    check("**/bin/" in ignore, ".dockerignore does not exclude **/bin/")
    check("**/obj/" in ignore, ".dockerignore does not exclude **/obj/")
    check("vendor" not in ignore, ".dockerignore excludes the vendor directory")


def check_quadlet() -> None:
    quadlet = read(ROOT / "pkgsrv.container")
    required = [
        "Image=localhost/pkgsrv:latest",
        "ContainerName=pkgsrv",
        "Network=host",
        "UserNS=keep-id:uid=1654,gid=1654",
        "Volume=/sock:/run/pkgsrv",
        "Volume=/var/lib/pkgsrv/artifacts:/var/lib/pkgsrv/artifacts",
        "Volume=/etc/pkgsrv:/etc/pkgsrv:ro",
        "Environment=Pkgsrv__Listen__UnixSocketPath=/run/pkgsrv/pkgsrv.sock",
        "Environment=Pkgsrv__Artifacts__Root=/var/lib/pkgsrv/artifacts",
        "Environment=Pkgsrv__Signing__PrivateKeyPath=/etc/pkgsrv/signing-private.pem",
        "Environment=Pkgsrv__Signing__PublicKeyPath=/etc/pkgsrv/signing-public.pem",
        "Environment=Pkgsrv__Build__Enabled=false",
        "Environment=Pkgsrv__Database__Provider=Postgres",
        "Environment=Pkgsrv__Database__InitializeSchema=true",
        "Environment=Pkgsrv__Irva__Enabled=true",
        "Environment=Pkgsrv__Irva__Required=true",
        "EnvironmentFile=/home/deploy/.config/pkgsrv.env",
        "Restart=always",
        "UMask=0007",
    ]
    for needle in required:
        check(needle in quadlet, f"Quadlet missing: {needle}")
    check("{{PORT}}" not in quadlet, "Quadlet must not require a TCP port")
    check("PublishPort" not in quadlet, "Quadlet must not publish a TCP port")
    for key in ("AppToken", "ConnectionString", "Password"):
        check(
            not re.search(rf"^Environment=.*{key}=.+$", quadlet, re.MULTILINE),
            f"Quadlet embeds a secret-like {key} value",
        )


def check_environment_example() -> None:
    env_file = ROOT / "pkgsrv.env.example"
    assignments = {}
    for line in read(env_file).splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        assignments[key] = value
    for key in (
        "Pkgsrv__Database__ConnectionString",
        "Pkgsrv__Irva__BaseUrl",
        "Pkgsrv__Irva__AppToken",
    ):
        check(key in assignments, f"env example missing required key: {key}")
    for key, value in assignments.items():
        check(value == "", f"env example must not contain a value for {key}")


def check_entrypoint() -> None:
    entrypoint = ROOT / "entrypoint.sh"
    check(entrypoint.stat().st_mode & 0o111 != 0, "entrypoint.sh is not executable")
    syntax = subprocess.run(["sh", "-n", str(entrypoint)], capture_output=True, text=True)
    check(syntax.returncode == 0, f"entrypoint.sh syntax error: {syntax.stderr.strip()}")
    body = read(entrypoint)
    for needle in ("-S ", "stat -c '%u'", "rm -f --", "exec dotnet /app/Pkgsrv.Presentation.dll"):
        check(needle in body, f"entrypoint.sh missing stale-socket guard: {needle}")


def check_no_secrets() -> None:
    checked = [
        ROOT / "Dockerfile",
        ROOT / ".dockerignore",
        ROOT / "entrypoint.sh",
        ROOT / "pkgsrv.container",
        ROOT / "pkgsrv.env.example",
        ROOT / "DEPLOY.md",
    ]
    for path in checked:
        body = read(path)
        check("-----BEGIN" not in body, f"{path.name} contains private key material")
        check(
            not re.search(r"(?:AppToken|ConnectionString|Password)=[^<\s][^\s]*", body),
            f"{path.name} contains a non-placeholder secret assignment",
        )


def check_docs() -> None:
    docs = read(ROOT / "DEPLOY.md")
    for needle in (
        "/sock/pkgsrv.sock",
        "setfacl",
        "keep-id",
        "InitializeSchema=true",
        "Build__Enabled",
        "signing-private.pem",
        "Rollback",
    ):
        check(needle in docs, f"DEPLOY.md missing provisioning/rollback detail: {needle}")


def main() -> int:
    check_vendor()
    check_dockerfile()
    check_quadlet()
    check_environment_example()
    check_entrypoint()
    check_no_secrets()
    check_docs()
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("ok: pkgsrv deploy assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
