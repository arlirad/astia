#!/usr/bin/env python3
"""Derive the Astia project version from Git metadata.

The result is deterministic for a given repository state. It contains no
absolute paths and no timestamps; when Git metadata is unavailable the
``exported`` fallback is used instead.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

FALLBACK_VERSION = (0, 1, 0)
RELEASE_TAG = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SHA = re.compile(r"^[0-9a-fA-F]{7,64}$")

_NUMERIC_ID = r"0|[1-9][0-9]*"
_PRERELEASE_ID = rf"(?:{_NUMERIC_ID}|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
_BUILD_ID = r"[0-9A-Za-z-]+"
SEMVER = re.compile(
    rf"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    rf"(?:-({_PRERELEASE_ID}(?:\.{_PRERELEASE_ID})*))?"
    rf"(?:\+({_BUILD_ID}(?:\.{_BUILD_ID})*))?$"
)


class VersionError(ValueError):
    pass


class GitUnavailable(Exception):
    pass


def _git(root, *args):
    try:
        return subprocess.run(
            ["git", "-C", str(root), *args],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as error:
        raise GitUnavailable from error


def git_metadata(root):
    root = Path(root)
    top = _git(root, "rev-parse", "--show-toplevel")
    if top.returncode != 0:
        if (root / ".git").exists():
            raise VersionError("unable to read Git repository metadata")
        return None
    repository = Path(top.stdout.strip())

    def git(*args):
        result = _git(repository, *args)
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise VersionError(f"git {' '.join(args)} failed: {detail}")
        return result.stdout.strip()

    sha = git("rev-parse", "HEAD")
    if not SHA.fullmatch(sha):
        raise VersionError("Git HEAD is not a hexadecimal commit id")
    branch = ""
    symbolic = _git(repository, "symbolic-ref", "--quiet", "--short", "HEAD")
    if symbolic.returncode == 0:
        branch = symbolic.stdout.strip()
    dirty = bool(git("status", "--porcelain"))
    shallow = git("rev-parse", "--is-shallow-repository") == "true"
    commits = int(git("rev-list", "--count", "HEAD"))
    tags = []
    for tag in git("tag", "--merged", "HEAD").splitlines():
        if not tag:
            continue
        if (
            tag.startswith("v")
            and tag[1:2].isdigit()
            and not RELEASE_TAG.fullmatch(tag)
        ):
            raise VersionError(f"malformed release tag: {tag!r}")
        if RELEASE_TAG.fullmatch(tag):
            distance = int(git("rev-list", "--count", f"{tag}..HEAD"))
            tags.append({"tag": tag, "distance": distance})
    return {
        "sha": sha.lower(),
        "branch": branch,
        "dirty": dirty,
        "shallow": shallow,
        "commits": commits,
        "tags": tags,
    }


def _metadata_sha(metadata):
    sha = str(metadata.get("sha", "")).lower()
    if not SHA.fullmatch(sha):
        raise VersionError("Git metadata must contain a hexadecimal commit id")
    return sha


def _metadata_integer(metadata, key):
    value = metadata.get(key, 0)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise VersionError(f"Git metadata {key} must be a non-negative integer")
    return value


def _release_candidates(tags):
    if not isinstance(tags, list):
        raise VersionError("Git metadata tags must be a list")
    candidates = []
    for item in tags:
        if not isinstance(item, dict):
            raise VersionError("Git metadata tag entries must be objects")
        tag = item.get("tag")
        if not isinstance(tag, str):
            raise VersionError("Git metadata tag names must be strings")
        if (
            tag.startswith("v")
            and tag[1:2].isdigit()
            and not RELEASE_TAG.fullmatch(tag)
        ):
            raise VersionError(f"malformed release tag: {tag!r}")
        match = RELEASE_TAG.fullmatch(tag)
        if not match:
            continue
        distance = item.get("distance")
        if isinstance(distance, bool) or not isinstance(distance, int) or distance < 0:
            raise VersionError("tag distance must be a non-negative integer")
        candidates.append((tuple(map(int, match.groups())), tag, distance))
    return candidates


def _branch_marker(branch):
    if not branch or branch in {"main", "master", "HEAD"}:
        return "dev"
    marker = re.sub(r"[^0-9A-Za-z-]+", "-", branch).strip("-").lower()
    if not marker:
        return "dev"
    if marker.isdigit():
        marker = f"b{marker}"
    return marker


def _compose(parts, marker, distance, build):
    base = ".".join(str(part) for part in parts)
    version = f"{base}-{marker}.{distance}"
    if build:
        version += f"+{build}"
    return version


def _package_version(parts, marker, distance, sha, dirty):
    base = ".".join(str(part) for part in parts)
    if not marker:
        return base
    package = f"{base}-{marker}.{distance}.g{sha[:12]}"
    if dirty:
        package += ".dirty"
    return package


def _result(parts, version, package, pre_release, build, **fields):
    result = {
        "version": version,
        "package_version": package,
        "major": parts[0],
        "minor": parts[1],
        "patch": parts[2],
        "pre_release": pre_release,
        "build": build,
    }
    result.update(fields)
    return result


def derive_override(value):
    match = SEMVER.fullmatch(value)
    if not match:
        raise VersionError(f"invalid ASTIA_VERSION_OVERRIDE: {value!r}")
    parts = tuple(int(match.group(index)) for index in (1, 2, 3))
    pre_release = match.group(4) or ""
    build = match.group(5) or ""
    package = value.split("+", 1)[0]
    return _result(
        parts,
        value,
        package,
        pre_release,
        build,
        distance=0,
        sha="",
        branch="",
        tag="",
        dirty=False,
        release=not pre_release,
        override=True,
        shallow=False,
        source="override",
    )


def derive_exported():
    parts = FALLBACK_VERSION
    marker = "dev"
    version = _compose(parts, marker, 0, "unknown")
    package = _package_version(parts, marker, 0, "unknown", False)
    return _result(
        parts,
        version,
        package,
        f"{marker}.0",
        "unknown",
        distance=0,
        sha="",
        branch="",
        tag="",
        dirty=False,
        release=False,
        override=False,
        shallow=False,
        source="exported",
    )


def derive(metadata, override=None):
    if override:
        return derive_override(override)
    if metadata is None:
        return derive_exported()

    sha = _metadata_sha(metadata)
    commits = _metadata_integer(metadata, "commits")
    branch = str(metadata.get("branch") or "")
    dirty = bool(metadata.get("dirty", False))
    shallow = bool(metadata.get("shallow", False))
    candidates = _release_candidates(metadata.get("tags", []))
    if candidates:
        parts, tag, distance = max(candidates, key=lambda item: (item[0], item[1]))
        release = distance == 0 and not dirty
    else:
        parts = FALLBACK_VERSION
        tag = ""
        distance = commits
        release = False

    marker = "" if release else _branch_marker(branch)
    build = "" if release else sha[:12] + (".dirty" if dirty else "")
    version = (
        ".".join(str(part) for part in parts)
        if release
        else _compose(parts, marker, distance, build)
    )
    package = _package_version(
        parts, "" if release else marker, distance, sha, dirty and not release
    )
    return _result(
        parts,
        version,
        package,
        "" if release else f"{marker}.{distance}",
        build,
        distance=distance,
        sha=sha,
        branch=branch,
        tag=tag,
        dirty=dirty,
        release=release,
        override=False,
        shallow=shallow,
        source="git",
    )


def load_metadata(path):
    data = json.loads(Path(path).read_text())
    if not isinstance(data, dict):
        raise VersionError("Git metadata must be a JSON object")
    return data


def write_result(path, result):
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f"{output.name}.tmp")
    temporary.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    os.replace(temporary, output)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--metadata-json")
    parser.add_argument("--output", required=True)
    parser.add_argument(
        "--override", default=os.environ.get("ASTIA_VERSION_OVERRIDE", "")
    )
    parser.add_argument("--reject-dirty", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.override:
            metadata = None
        elif args.metadata_json:
            metadata = load_metadata(args.metadata_json)
        else:
            try:
                metadata = git_metadata(args.root)
            except GitUnavailable:
                metadata = None
        result = derive(metadata, args.override)
        if args.reject_dirty and (result["dirty"] or not result["release"]):
            raise VersionError("release builds require a clean exact release tag")
        write_result(args.output, result)
    except (OSError, VersionError, json.JSONDecodeError, TypeError) as error:
        print(f"astia-version: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
