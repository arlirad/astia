#!/usr/bin/env python3

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import subprocess
import sys


TRAILER_RE = re.compile(r"^\s*co-authored-by\s*:\s*\S.*$", re.IGNORECASE)
ZERO_SHA_RE = re.compile(r"^0{40,64}$")
WORKFLOW_EVENTS = ("pull_request", "merge_group")
GIT_TIMEOUT_SECONDS = 120


class RangeError(Exception):
    pass


@dataclasses.dataclass(frozen=True)
class Commit:
    sha: str
    subject: str
    body: str

    @property
    def message(self):
        return f"{self.subject}\n{self.body}"


@dataclasses.dataclass(frozen=True)
class CommitRange:
    base: str
    head: str
    single: bool = False

    @property
    def label(self):
        return self.head if self.single else f"{self.base}..{self.head}"

    def revision_args(self):
        return [self.head] if self.single else [f"{self.base}..{self.head}"]


def git_result(args):
    try:
        return subprocess.run(
            ["git", *args],
            capture_output=True,
            text=True,
            timeout=GIT_TIMEOUT_SECONDS,
        )
    except FileNotFoundError as error:
        raise RangeError("git is not installed") from error
    except subprocess.TimeoutExpired as error:
        raise RangeError(f"git {' '.join(args)} timed out") from error


def git(args):
    result = git_result(args)
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "git failed"
        raise RangeError(message)
    return result.stdout


def resolve_commit(revision):
    result = git_result(
        ["rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}"]
    )
    resolved = result.stdout.strip()
    if result.returncode != 0 or not resolved:
        raise RangeError(f"unknown revision {revision!r}")
    return resolved


def parent_range(head):
    try:
        parent = git(["rev-parse", "--verify", "--quiet", f"{head}^"]).strip()
    except RangeError:
        parent = ""
    if parent:
        return CommitRange(parent, head)
    return CommitRange(head, head, single=True)


def resolve_range(selector):
    if selector:
        base, separator, head = selector.partition("..")
        if not separator or not base or not head or ".." in head:
            raise RangeError(
                f"invalid --range {selector!r}; expected a BASE..HEAD range"
            )
        return CommitRange(resolve_commit(base), resolve_commit(head))

    event = os.environ.get("GITHUB_EVENT_NAME", "")
    base = os.environ.get("BASE_SHA", "").strip()
    head = os.environ.get("HEAD_SHA", "").strip()
    if event in WORKFLOW_EVENTS:
        if not base or not head:
            raise RangeError(f"{event} requires BASE_SHA and HEAD_SHA")
        return CommitRange(resolve_commit(base), resolve_commit(head))
    if base and not ZERO_SHA_RE.match(base):
        return CommitRange(resolve_commit(base), resolve_commit(head or "HEAD"))
    return parent_range(resolve_commit(head or "HEAD"))


def iter_commits(commit_range):
    output = git(
        [
            "log",
            "--no-show-signature",
            "--format=%H%x00%s%x00%b%x00",
            *commit_range.revision_args(),
        ]
    )
    fields = output.split("\x00")
    index = 0
    while index + 2 < len(fields):
        sha = fields[index].strip("\n")
        if not sha:
            break
        yield Commit(sha=sha, subject=fields[index + 1], body=fields[index + 2])
        index += 3


def find_trailers(message):
    return [
        line.rstrip() for line in message.splitlines() if TRAILER_RE.match(line)
    ]


def check_range(commit_range):
    offenders = 0
    for commit in iter_commits(commit_range):
        trailers = find_trailers(commit.message)
        if not trailers:
            continue
        print(f"{commit.sha} {commit.subject}")
        for trailer in trailers:
            print(trailer)
        offenders += len(trailers)
    if offenders:
        print(
            "commit attribution: "
            f"{offenders} forbidden co-author trailer(s) in {commit_range.label}"
        )
        return 1
    print(
        "commit attribution: no forbidden co-author trailers "
        f"in {commit_range.label}"
    )
    return 0


SELF_TEST_CASES = (
    (
        "clean message",
        "Explain the change.\n\nSigned-off-by: A Person <a@example.com>",
        (),
    ),
    (
        "mixed-case trailer",
        "Body text.\n\nCO-AUTHORED-BY: Real Person <person@example.com>",
        ("CO-AUTHORED-BY: Real Person <person@example.com>",),
    ),
    (
        "trailer with extra spaces",
        "Body text.\n\n   Co-authored-by :   Space Person <space@example.com>",
        ("   Co-authored-by :   Space Person <space@example.com>",),
    ),
    (
        "prose mentioning the phrase",
        "Documentation mentions Co-authored-by: in prose only.\n"
        "The phrase Co-authored-by: is not a trailer here.",
        (),
    ),
    (
        "multiple trailers",
        "Body text.\n\nCo-Authored-By: A Person <a@example.com>\n"
        "Signed-off-by: B Person <b@example.com>\n"
        "co-authored-by: C Person <c@example.com>",
        (
            "Co-Authored-By: A Person <a@example.com>",
            "co-authored-by: C Person <c@example.com>",
        ),
    ),
)


def run_self_test():
    failures = 0
    for name, body, expected in SELF_TEST_CASES:
        actual = tuple(find_trailers(body))
        if actual != expected:
            print(
                f"self-test {name}: expected {expected!r}, got {actual!r}",
                file=sys.stderr,
            )
            failures += 1
    if failures:
        print(f"commit attribution self-test: {failures} failure(s)", file=sys.stderr)
        return 1
    print("commit attribution self-test: passed")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fail when a commit range contains a co-author trailer."
    )
    parser.add_argument(
        "--range",
        dest="selector",
        metavar="BASE..HEAD",
        help="check an explicit range instead of resolving one from the workflow env",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run embedded trailer-matching fixtures without git",
    )
    args = parser.parse_args(argv)
    if args.self_test:
        return run_self_test()
    try:
        commit_range = resolve_range(args.selector)
        return check_range(commit_range)
    except RangeError as error:
        print(f"commit attribution: error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
