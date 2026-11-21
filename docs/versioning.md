# Versioning prototype

Astia versions are derived by `tools/astia_version.py` from annotated or
lightweight release tags named `vMAJOR.MINOR.PATCH`. A clean commit exactly at
a release tag is `MAJOR.MINOR.PATCH`; commits after it use
`MAJOR.MINOR.PATCH-dev.DISTANCE+SHA` on `main`, or a sanitized branch name in
place of `dev` on other branches. Without a reachable release tag, the base is
`0.0.0`. Dirty tracked worktrees receive a `.dirty` suffix. CI can pass
`--reject-dirty` to require a clean exact release tag.

Set `ASTIA_VERSION_OVERRIDE` to an explicit SemVer value to build offline or
reproducibly without Git metadata. Configure-time output is written only under
the build directory as `generated/astia-version.json`; CMake derives its
project version, generated kernel/userspace headers, and package version from
that artifact. For shallow clones, distance is the number of commits available
in the clone when no release tag is reachable. CI should fetch complete history
for authoritative distances.

The prototype can be checked with
`python3 tools/test_astia_version.py`. Test metadata fixtures are supported via
`--metadata-json` and do not modify a Git repository.
