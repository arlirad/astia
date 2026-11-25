# Versioning

Astia derives its project version from Git metadata with
`tools/astia_version.py`. The generated artifact contains no timestamps and no
absolute paths, so the same source state always produces the same bytes. The
script is the single source of truth for the repository version; do not add
hard-coded version numbers to build files or headers.

## Precedence

The version is selected in this order:

1. `ASTIA_VERSION_OVERRIDE` (environment or CMake cache variable) or
   `--override` to the script. The value must be a complete SemVer 2.0.0
   version. No Git repository is required, so this is the offline and
   reproducible-build escape hatch.
2. The highest reachable release tag named `vMAJOR.MINOR.PATCH`. Annotated and
   lightweight tags are both accepted; annotated tags are peeled to their
   commit. "Highest" is numeric SemVer order, not tag distance. The distance is
   the number of commits from the selected tag to `HEAD`.
3. A Git repository with no reachable release tag uses the fallback base
   `0.1.0` and the total number of commits as its distance. `0.1.0` is the
   version already declared by the kernel and libc projects; the fallback does
   not claim a release.
4. A source tree without Git metadata uses the deterministic exported fallback
   `0.1.0-dev.0+unknown`.

Tags that do not look like releases (for example `backup/pre-identity`) are
ignored. A tag beginning with `v` followed by a digit but not matching the exact
`vMAJOR.MINOR.PATCH` form is invalid and makes version derivation fail instead
of being silently ignored. Malformed or unreadable repository metadata also
fails; an absent Git repository uses the exported fallback.

## Version format

A clean commit exactly at the selected release tag is:

```text
MAJOR.MINOR.PATCH
```

Any other state is a SemVer pre-release with build metadata:

```text
MAJOR.MINOR.PATCH-MARKER.DISTANCE+SHA[.dirty]
```

* `MARKER` is `dev` on `main`, `master`, and detached heads. On another branch
  it is the branch name lowercased, with runs of characters outside
  `[0-9A-Za-z-]` replaced by `-`, and leading/trailing `-` removed. A marker
  consisting only of digits is prefixed with `b` so it remains a valid
  non-numeric pre-release identifier.
* `DISTANCE` is the number of commits from the selected release tag to `HEAD`,
  or the total commit count when no release tag is reachable.
* `SHA` is the first 12 lowercase hexadecimal characters of `HEAD`.
* `.dirty` is appended to the build metadata when the worktree has tracked or
  untracked non-ignored changes. A dirty tree is never a release, even at an
  exact tag.

Examples on `main`:

```text
2.4.6                                  clean exact v2.4.6 tag
2.4.6-dev.3+0123456789ab               three commits after v2.4.6
2.4.6-feature-new-ui.3+0123456789ab    same distance on feature/new-ui
2.4.6-dev.0+0123456789ab.dirty         dirty tree at v2.4.6
0.1.0-dev.12+0123456789ab              no reachable release tag
0.1.0-dev.0+unknown                    exported source without Git metadata
```

`package_version` in the generated JSON is the equivalent version accepted by
the `pkg` prototype, which does not allow `+` build metadata:

```text
MAJOR.MINOR.PATCH[-MARKER.DISTANCE.gSHA[.dirty]]
```

For example, `1.0.0-dev.2+0123456789ab` has package version
`1.0.0-dev.2.g0123456789ab`.

## Build, runtime, and package exposure

`cmake/AstiaVersion.cmake` runs the script at configure time and writes:

* `<build>/generated/astia-version.json`: the complete result, including
  `version`, `package_version`, numeric components, `pre_release`, `build`,
  `distance`, `sha`, `branch`, `tag`, `dirty`, `release`, `source`, and
  `shallow`.
* `<build>/generated/include/astia/version.h` and `version.hpp`: the
  `ASTIA_VERSION_*` macros used by the kernel and libc builds.

The CMake project version, the kernel `astia::core` version constants, the
generated headers, and the `ASTIA_PACKAGE_VERSION` cache variable all come from
the same artifact. Libc installs the generated headers with the rest of its
public headers. The kernel and libc remain independently configurable and
discover the enclosing Git repository when their source directory is a
subdirectory of it.

Set `ASTIA_VERSION_REQUIRE_RELEASE=ON` (or pass `--reject-dirty`) to fail
configuration unless the version is a clean exact release tag. Kernel CI (x86_64) runs
that check for `v*` tag pushes. Use a full-history checkout for authoritative
distances; a shallow clone can only count the commits it contains, and a
release tag outside the clone is invisible.

## Checking

Run the focused suite with:

```sh
python3 tools/test_astia_version.py
```

It covers annotated and lightweight release tags, commits after a tag, feature
branch markers, dirty trees, release rejection, no-tag repositories, shallow
clones, exported source, reproducible output, overrides, malformed tags, and
invalid repository/metadata input.
