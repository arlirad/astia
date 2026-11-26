#!/usr/bin/env python3
"""Fixtures for .github/scripts/check_attribution.py."""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile


CHECKER = pathlib.Path(__file__).with_name("check_attribution.py")


def git(root, *args, input_text=None):
    return subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        capture_output=True,
        text=True,
        input=input_text,
    )


def init_repo(root):
    git(root, "init", "--quiet", "--initial-branch=main")
    git(root, "config", "user.name", "Attribution Fixture")
    git(root, "config", "user.email", "attribution@example.com")
    git(root, "config", "commit.gpgsign", "false")


def commit(root, subject, body=""):
    message = subject if not body else f"{subject}\n\n{body}"
    git(
        root,
        "commit",
        "--allow-empty",
        "--no-gpg-sign",
        "--quiet",
        "-F",
        "-",
        input_text=message,
    )
    return git(root, "rev-parse", "HEAD").stdout.strip()


def run(root, *args, env=None):
    return subprocess.run(
        [sys.executable, str(CHECKER), *args],
        cwd=root,
        capture_output=True,
        text=True,
        env=env,
    )


def expect_clean(root, base, head):
    result = run(root, "--range", f"{base}..{head}")
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "no forbidden co-author trailers" in result.stdout, result.stdout


def main():
    with tempfile.TemporaryDirectory(prefix="attribution-fixture-") as tmp:
        root = pathlib.Path(tmp)
        init_repo(root)
        base = commit(root, "base commit", "Initial fixture state.")
        clean = commit(
            root,
            "clean change",
            "Explain the change.\n\nSigned-off-by: A Person <a@example.com>",
        )
        mixed = commit(
            root,
            "mixed case trailer",
            "CO-AUTHORED-BY: Mixed Person <mixed@example.com>",
        )
        spaced = commit(
            root,
            "spaced trailer",
            "   Co-authored-by :   Space Person <space@example.com>",
        )
        prose = commit(
            root,
            "prose only",
            "Documentation mentions Co-authored-by: in prose only.\n"
            "The phrase Co-authored-by: is not a trailer here.",
        )
        first = commit(
            root,
            "first offender",
            "Co-Authored-By: First Person <first@example.com>",
        )
        second = commit(
            root,
            "second offender",
            "co-authored-by : Second Person <second@example.com>",
        )

        expect_clean(root, base, clean)

        result = run(root, "--range", f"{clean}..{mixed}")
        assert result.returncode == 1, (result.stdout, result.stderr)
        assert result.stdout == (
            f"{mixed} mixed case trailer\n"
            "CO-AUTHORED-BY: Mixed Person <mixed@example.com>\n"
            f"commit attribution: 1 forbidden co-author trailer(s) in "
            f"{clean}..{mixed}\n"
        ), result.stdout

        result = run(root, "--range", f"{mixed}..{spaced}")
        assert result.returncode == 1, (result.stdout, result.stderr)
        assert (
            "   Co-authored-by :   Space Person <space@example.com>"
            in result.stdout
        ), result.stdout

        expect_clean(root, spaced, prose)

        result = run(root, "--range", f"{prose}..{second}")
        assert result.returncode == 1, (result.stdout, result.stderr)
        assert result.stdout == (
            f"{second} second offender\n"
            "co-authored-by : Second Person <second@example.com>\n"
            f"{first} first offender\n"
            "Co-Authored-By: First Person <first@example.com>\n"
            f"commit attribution: 2 forbidden co-author trailer(s) in "
            f"{prose}..{second}\n"
        ), result.stdout

        result = run(root, "--range", f"{'0' * 40}..{second}")
        assert result.returncode == 2, (result.stdout, result.stderr)
        assert "unknown revision" in result.stderr, result.stderr
        assert "commit attribution: error:" in result.stderr, result.stderr

        missing = run(
            root,
            env={
                **os.environ,
                "GITHUB_EVENT_NAME": "pull_request",
                "BASE_SHA": "",
                "HEAD_SHA": "",
            },
        )
        assert missing.returncode == 2, (missing.stdout, missing.stderr)
        assert "requires BASE_SHA and HEAD_SHA" in missing.stderr, missing.stderr

    print("attribution checker test: passed")


if __name__ == "__main__":
    main()
