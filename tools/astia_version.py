#!/usr/bin/env python3
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

RELEASE_TAG = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SHA = re.compile(r"^[0-9a-fA-F]{7,40}$")


class VersionError(ValueError):
    pass


def git_metadata(root):
    def git(*args):
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True, stderr=subprocess.PIPE
        ).strip()

    sha = os.environ.get("GITHUB_SHA") or git("rev-parse", "HEAD")
    branch = os.environ.get("GITHUB_HEAD_REF") or os.environ.get("GITHUB_REF_NAME")
    if not branch:
        try:
            branch = git("symbolic-ref", "--quiet", "--short", "HEAD")
        except subprocess.CalledProcessError:
            branch = ""
    dirty = bool(git("status", "--porcelain"))
    shallow = git("rev-parse", "--is-shallow-repository") == "true"
    tags = []
    for tag in git("tag", "--merged", "HEAD").splitlines():
        if tag.startswith("v") and re.match(r"^v[0-9]", tag):
            try:
                distance = int(git("rev-list", "--count", f"{tag}..HEAD"))
            except subprocess.CalledProcessError:
                continue
            tags.append({"tag": tag, "distance": distance})
    tags.sort(key=lambda item: (item["distance"], item["tag"]))
    try:
        commits = int(git("rev-list", "--count", "HEAD"))
    except subprocess.CalledProcessError:
        commits = 1
    return {
        "sha": sha,
        "branch": branch,
        "dirty": dirty,
        "shallow": shallow,
        "commits": commits,
        "tags": tags,
    }


def derive(metadata, override=None):
    if override:
        match = re.fullmatch(
            r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:[-+][0-9A-Za-z.-]+)?",
            override,
        )
        if not match:
            raise VersionError(f"invalid ASTIA_VERSION_OVERRIDE: {override!r}")
        parts = tuple(int(part) for part in match.groups() if part is not None)
        return {
            "version": override,
            "major": parts[0],
            "minor": parts[1],
            "patch": parts[2],
            "distance": 0,
            "sha": "",
            "branch": "",
            "dirty": False,
            "release": "-" not in override and "+" not in override,
            "override": True,
            "shallow": False,
        }

    sha = str(metadata.get("sha", "")).lower()
    if not SHA.fullmatch(sha):
        raise VersionError("Git metadata must contain a full or short hexadecimal sha")
    tags = metadata.get("tags", [])
    for item in tags:
        tag = item["tag"]
        if (
            tag.startswith("v")
            and re.match(r"^v[0-9]", tag)
            and not RELEASE_TAG.fullmatch(tag)
        ):
            raise VersionError(f"malformed release tag: {tag!r}")
    candidates = []
    for item in tags:
        match = RELEASE_TAG.fullmatch(item["tag"])
        if match:
            distance = int(item["distance"])
            if distance < 0:
                raise VersionError("tag distance must not be negative")
            candidates.append((distance, item["tag"], tuple(map(int, match.groups()))))
    if candidates:
        distance, _, parts = min(candidates, key=lambda entry: (entry[0], entry[1]))
    else:
        parts = (0, 0, 0)
        distance = int(metadata.get("commits", 1))
    branch = str(metadata.get("branch") or "")
    marker = (
        "dev"
        if not branch or branch in {"main", "master"}
        else re.sub(r"[^0-9A-Za-z-]+", "-", branch.replace("/", "-")).strip("-").lower()
    )
    version = f"{parts[0]}.{parts[1]}.{parts[2]}"
    release = bool(candidates) and distance == 0 and not metadata.get("dirty", False)
    if not release:
        version += f"-{marker}.{distance}+{sha[:12]}"
    if metadata.get("dirty", False):
        version += ".dirty"
    return {
        "version": version,
        "major": parts[0],
        "minor": parts[1],
        "patch": parts[2],
        "distance": distance,
        "sha": sha,
        "branch": branch,
        "dirty": bool(metadata.get("dirty", False)),
        "release": release,
        "override": False,
        "shallow": bool(metadata.get("shallow", False)),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--metadata-json")
    parser.add_argument("--output", required=True)
    parser.add_argument(
        "--override", default=os.environ.get("ASTIA_VERSION_OVERRIDE", "")
    )
    parser.add_argument("--reject-dirty", action="store_true")
    args = parser.parse_args()
    try:
        if args.metadata_json:
            metadata = json.loads(Path(args.metadata_json).read_text())
        elif args.override:
            metadata = {}
        else:
            metadata = git_metadata(args.root)
        result = derive(metadata, args.override)
        if args.reject_dirty and (result["dirty"] or not result["release"]):
            raise VersionError("release builds require a clean exact release tag")
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    except (
        OSError,
        KeyError,
        TypeError,
        ValueError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"astia-version: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
