import contextlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from astia_version import VersionError, derive, main

SCRIPT = Path(__file__).with_name("astia_version.py")
SHA = "0123456789abcdef0123456789abcdef01234567"


def metadata(**updates):
    data = {
        "sha": SHA,
        "branch": "main",
        "dirty": False,
        "shallow": False,
        "commits": 4,
        "tags": [],
    }
    data.update(updates)
    return data


class GitRepository:
    def __init__(self, path):
        self.path = Path(path)
        self.path.mkdir(parents=True)
        self.git("init", "-q")
        self.git("symbolic-ref", "HEAD", "refs/heads/main")
        self.git("config", "user.name", "Astia Test")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "commit.gpgsign", "false")

    def git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.path), *args],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.strip()

    def write(self, content, name="tracked.txt"):
        (self.path / name).write_text(content)

    def commit(self, message):
        self.write(message)
        self.git("add", "tracked.txt")
        self.git("commit", "-q", "-m", message)
        return self.head()

    def head(self):
        return self.git("rev-parse", "HEAD")

    def tag(self, name, annotated=False):
        if annotated:
            self.git("tag", "-a", name, "-m", name)
        else:
            self.git("tag", name)


def run_cli(root, *args):
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "astia-version.json"
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--root",
                str(root),
                "--output",
                str(output),
                *args,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        data = json.loads(output.read_text()) if output.exists() else None
        raw = output.read_bytes() if output.exists() else b""
        return completed, data, raw


class VersionTests(unittest.TestCase):
    def test_annotated_release_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            sha = repository.commit("initial")
            repository.tag("v2.4.6", annotated=True)
            completed, result, _ = run_cli(repository.path)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(result["version"], "2.4.6")
            self.assertEqual(result["package_version"], "2.4.6")
            self.assertEqual(result["tag"], "v2.4.6")
            self.assertEqual(result["sha"], sha)
            self.assertEqual(result["distance"], 0)
            self.assertTrue(result["release"])
            self.assertEqual(result["source"], "git")

            completed, _, _ = run_cli(repository.path, "--reject-dirty")
            self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_lightweight_release_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.2.3")
            _, result, _ = run_cli(repository.path)
            self.assertEqual(result["version"], "1.2.3")
            self.assertTrue(result["release"])

    def test_commits_after_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.0.0", annotated=True)
            repository.commit("second")
            sha = repository.commit("third")
            _, result, _ = run_cli(repository.path)
            short = sha[:12]
            self.assertEqual(result["version"], f"1.0.0-dev.2+{short}")
            self.assertEqual(result["package_version"], f"1.0.0-dev.2.g{short}")
            self.assertEqual(result["distance"], 2)
            self.assertEqual(result["pre_release"], "dev.2")
            self.assertFalse(result["release"])

    def test_feature_branch_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.0.0", annotated=True)
            repository.git("checkout", "-q", "-b", "feature/new-ui")
            sha = repository.commit("feature")
            _, result, _ = run_cli(repository.path)
            self.assertEqual(result["version"], f"1.0.0-feature-new-ui.1+{sha[:12]}")

    def test_numeric_branch_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.0.0", annotated=True)
            repository.git("checkout", "-q", "-b", "123")
            sha = repository.commit("numeric")
            _, result, _ = run_cli(repository.path)
            self.assertEqual(result["version"], f"1.0.0-b123.1+{sha[:12]}")

    def test_dirty_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            sha = repository.commit("initial")
            repository.tag("v1.0.0", annotated=True)
            repository.write("modified")
            _, result, _ = run_cli(repository.path)
            short = sha[:12]
            self.assertEqual(result["version"], f"1.0.0-dev.0+{short}.dirty")
            self.assertEqual(result["package_version"], f"1.0.0-dev.0.g{short}.dirty")
            self.assertTrue(result["dirty"])
            self.assertFalse(result["release"])
            completed, _, _ = run_cli(repository.path, "--reject-dirty")
            self.assertEqual(completed.returncode, 1)
            self.assertIn("clean exact release tag", completed.stderr)

    def test_no_tag_uses_fallback_base(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            sha = repository.commit("second")
            _, result, _ = run_cli(repository.path)
            self.assertEqual(result["version"], f"0.1.0-dev.2+{sha[:12]}")
            self.assertEqual(result["tag"], "")
            self.assertEqual(result["distance"], 2)
            self.assertFalse(result["release"])

    def test_shallow_clone_without_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            origin = GitRepository(directory / "origin")
            origin.commit("initial")
            origin.commit("second")
            sha = origin.commit("third")
            clone = directory / "clone"
            subprocess.run(
                [
                    "git",
                    "clone",
                    "-q",
                    "--depth",
                    "1",
                    f"file://{origin.path}",
                    str(clone),
                ],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            _, result, _ = run_cli(clone)
            self.assertEqual(result["version"], f"0.1.0-dev.1+{sha[:12]}")
            self.assertTrue(result["shallow"])
            self.assertEqual(result["distance"], 1)

    def test_exported_source_is_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source = directory / "exported"
            source.mkdir()
            (source / "README").write_text("Astia exported source\n")
            first, result, raw = run_cli(source)
            self.assertEqual(first.returncode, 0, first.stderr)
            second, _, raw_again = run_cli(source)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(raw, raw_again)
            self.assertEqual(result["version"], "0.1.0-dev.0+unknown")
            self.assertEqual(result["package_version"], "0.1.0-dev.0.gunknown")
            self.assertEqual(result["source"], "exported")
            self.assertFalse(result["release"])
            self.assertNotIn(str(source), raw.decode())

    def test_override_is_offline_and_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "exported"
            source.mkdir()
            completed, result, _ = run_cli(source, "--override", "7.8.9")
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(result["version"], "7.8.9")
            self.assertEqual(result["package_version"], "7.8.9")
            self.assertEqual(result["source"], "override")
            self.assertTrue(result["release"])

            completed, result, _ = run_cli(source, "--override", "7.8.9-rc.1+build.5")
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(result["version"], "7.8.9-rc.1+build.5")
            self.assertEqual(result["package_version"], "7.8.9-rc.1")
            self.assertFalse(result["release"])

            completed, _, _ = run_cli(source, "--override", "v7.8")
            self.assertEqual(completed.returncode, 1)

    def test_highest_reachable_tag_wins(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.0.0")
            repository.commit("second")
            repository.tag("v2.0.0", annotated=True)
            sha = repository.commit("third")
            _, result, _ = run_cli(repository.path)
            self.assertEqual(result["tag"], "v2.0.0")
            self.assertEqual(result["version"], f"2.0.0-dev.1+{sha[:12]}")
            self.assertEqual(result["distance"], 1)

    def test_detached_head_uses_dev_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.0.0", annotated=True)
            repository.commit("second")
            repository.git("checkout", "-q", "--detach")
            sha = repository.head()
            _, result, _ = run_cli(repository.path)
            self.assertEqual(result["version"], f"1.0.0-dev.1+{sha[:12]}")
            self.assertEqual(result["branch"], "")

    def test_invalid_metadata_is_rejected(self):
        with self.assertRaises(VersionError):
            derive(metadata(sha="not-a-sha"))
        with self.assertRaises(VersionError):
            derive(metadata(tags=[{"tag": "v1.0.0", "distance": -1}]))
        with self.assertRaises(VersionError):
            derive(metadata(tags=[{"tag": "v1.2", "distance": 0}]))
        with self.assertRaises(VersionError):
            derive(metadata(tags=["v1.0.0"]))
        with self.assertRaises(VersionError):
            derive(metadata(commits=-1))

    def test_invalid_metadata_json_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            fixture = directory / "metadata.json"
            output = directory / "version.json"
            fixture.write_text("{not json")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(
                    main(
                        [
                            "--metadata-json",
                            str(fixture),
                            "--output",
                            str(output),
                        ]
                    ),
                    1,
                )
            self.assertFalse(output.exists())

    def test_malformed_release_tag_in_repository_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            repository = GitRepository(Path(directory) / "repo")
            repository.commit("initial")
            repository.tag("v1.2")
            completed, _, _ = run_cli(repository.path)
            self.assertEqual(completed.returncode, 1)
            self.assertIn("malformed release tag", completed.stderr)

    def test_broken_repository_metadata_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "broken"
            source.mkdir()
            (source / ".git").write_text("gitdir: missing\n")
            completed, _, _ = run_cli(source)
            self.assertEqual(completed.returncode, 1)
            self.assertIn("unable to read Git repository metadata", completed.stderr)

    def test_no_tag_metadata_fixture(self):
        result = derive(metadata())
        self.assertEqual(result["version"], f"0.1.0-dev.4+{SHA[:12]}")
        self.assertEqual(result["package_version"], f"0.1.0-dev.4.g{SHA[:12]}")

    def test_annotated_metadata_fixture(self):
        result = derive(metadata(tags=[{"tag": "v2.4.6", "distance": 0}]))
        self.assertEqual(result["version"], "2.4.6")
        self.assertTrue(result["release"])


if __name__ == "__main__":
    if shutil.which("git") is None:
        raise SystemExit("git is required for the version tests")
    unittest.main()
