#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

import hashlib
import importlib.machinery
import importlib.util
import io
import json
import subprocess
import tarfile
import tempfile
import urllib.request
import unittest
from pathlib import Path


PKG = Path(__file__).resolve().parents[1] / "pkg"
ARCH = "x86_64"
ABI = "linux-gnu"


def make_archive(repo, name, version, files, bad_manifest_path=None, archive_name=None):
    payload = []
    for path, content in files.items():
        data = content.encode()
        payload.append({
            "path": path,
            "sha256": hashlib.sha256(data).hexdigest(),
            "mode": 0o755 if path.startswith("bin/") else 0o644,
            "data": data,
        })
    manifest = {
        "format_schema": 1,
        "name": name,
        "version": version,
        "files": [{key: entry[key] for key in ("path", "sha256", "mode")} for entry in payload],
    }
    path = repo / f"{archive_name or name}-{version}.tar.gz"
    with tarfile.open(path, "w:gz") as archive:
        manifest_data = json.dumps(manifest, sort_keys=True).encode()
        info = tarfile.TarInfo("package.json")
        info.size = len(manifest_data)
        archive.addfile(info, io.BytesIO(manifest_data))
        for entry in payload:
            info = tarfile.TarInfo(entry["path"])
            info.mode = entry["mode"]
            info.size = len(entry["data"])
            archive.addfile(info, io.BytesIO(entry["data"]))
    if bad_manifest_path:
        with tarfile.open(path, "w:gz") as archive:
            manifest_data = json.dumps({**manifest, "files": [{**manifest["files"][0], "path": bad_manifest_path}]}).encode()
            info = tarfile.TarInfo("package.json")
            info.size = len(manifest_data)
            archive.addfile(info, io.BytesIO(manifest_data))
            for entry in payload:
                info = tarfile.TarInfo(entry["path"])
                info.size = len(entry["data"])
                archive.addfile(info, io.BytesIO(entry["data"]))
    return path


def write_index(repo, packages):
    index = {
        "schema": 1,
        "generated_at": "2026-01-01T00:00:00Z",
        "repository": {
            "name": "test",
            "channel": "stable",
            "architectures": ["x86_64", "aarch64"],
        },
        "packages": packages,
    }
    (repo / "index.json").write_text(json.dumps(index), encoding="utf-8")


def package_entry(repo, name, version, archive=None, dependencies=None, architecture=ARCH):
    archive = archive or make_archive(repo, name, version, {"bin/" + name: f"{name} {version}\n"})
    return {
        "artifacts": {
            architecture: {
                "archive": archive.name,
                "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                "size": archive.stat().st_size,
            }
        },
        "dependencies": dependencies or [],
    }


class PackageManagerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "root"
        self.repo = Path(self.temp.name) / "repo"
        self.repo.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def run_pkg(self, *args):
        return self.run_pkg_at(self.root, *args)

    def run_pkg_at(self, root, *args):
        return subprocess.run(
            [str(PKG), "--root", str(root), *args],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )

    def sync_package(self, name="hello", version="1.0.0", **kwargs):
        entry = package_entry(self.repo, name, version, **kwargs)
        write_index(self.repo, {name: {"versions": {version: entry}}})
        result = self.run_pkg("sync", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_sync_install_list_remove_and_idempotency(self):
        self.sync_package()
        result = self.run_pkg("-i", "hello")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "bin/hello").read_text(), "hello 1.0.0\n")
        result = self.run_pkg("list")
        self.assertEqual(result.stdout.strip(), "hello 1.0.0")
        result = self.run_pkg("-i", "hello", "1.0.0")
        self.assertIn("already installed", result.stdout)
        result = self.run_pkg("-r", "hello")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "bin/hello").exists())

    def test_version_selection(self):
        one = package_entry(self.repo, "hello", "1.0.0")
        two = package_entry(self.repo, "hello", "2.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": one, "2.0.0": two}}})
        result = self.run_pkg("sync", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg("install", "hello")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2.0.0", result.stdout)

    def test_checksum_mismatch(self):
        archive = make_archive(self.repo, "bad", "1.0.0", {"bin/bad": "bad"})
        entry = package_entry(self.repo, "bad", "1.0.0", archive=archive)
        entry["artifacts"][ARCH]["sha256"] = "0" * 64
        write_index(self.repo, {"bad": {"versions": {"1.0.0": entry}}})
        result = self.run_pkg("sync", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg("install", "bad")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "bin/bad").exists())

    def test_traversal_and_symlink_rejected(self):
        archive = make_archive(self.repo, "evil", "1.0.0", {"../escape": "bad"})
        entry = package_entry(self.repo, "evil", "1.0.0", archive=archive, dependencies=[])
        write_index(self.repo, {"evil": {"versions": {"1.0.0": entry}}})
        result = self.run_pkg("sync", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg("install", "evil")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((Path(self.temp.name) / "escape").exists())

    def test_clobber_and_rollback(self):
        self.sync_package()
        target = self.root / "bin/hello"
        target.parent.mkdir(parents=True)
        target.write_text("unmanaged")
        result = self.run_pkg("install", "hello")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_text(), "unmanaged")
        target.unlink()
        self.assertTrue(self.run_pkg("-i", "hello").returncode == 0)
        target.write_text("modified but still owned")
        result = self.run_pkg("-r", "hello")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(target.exists())

    def test_malformed_and_atomic_sync(self):
        self.sync_package()
        cached = self.root / ".pkg/cache.json"
        old = cached.read_bytes()
        (self.repo / "index.json").write_text("{", encoding="utf-8")
        result = self.run_pkg("-s", "--repo", str(self.repo))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(cached.read_bytes(), old)

    def test_immutable_version_and_dependencies_deferred(self):
        self.sync_package()
        entry = package_entry(self.repo, "hello", "1.0.0")
        entry["artifacts"][ARCH]["archive"] = "changed.tar.gz"
        write_index(self.repo, {"hello": {"versions": {"1.0.0": entry}}})
        result = self.run_pkg("-s", "--repo", str(self.repo))
        self.assertNotEqual(result.returncode, 0)
        entry = package_entry(self.repo, "dep", "1.0.0", dependencies=["other >= 1.0.0"])
        write_index(self.repo, {"dep": {"versions": {"1.0.0": entry}}})
        result = self.run_pkg("-s", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg("-i", "dep")
        self.assertNotEqual(result.returncode, 0)

    def test_multi_arch_selection_and_target_lock(self):
        x86 = package_entry(self.repo, "hello", "1.0.0")
        arm_archive = make_archive(self.repo, "hello", "1.0.0", {"bin/hello": "aarch64\n"}, archive_name="hello-arm")
        arm = package_entry(self.repo, "hello", "1.0.0", archive=arm_archive, architecture="aarch64")
        x86["artifacts"].update(arm["artifacts"])
        write_index(self.repo, {"hello": {"versions": {"1.0.0": x86}}})
        result = self.run_pkg("--arch", "aarch64", "-is", "hello", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "bin/hello").read_text(), "aarch64\n")
        result = self.run_pkg("--arch", "x86_64", "-i", "hello")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("targets aarch64", result.stderr)

    def test_incompatible_architecture_is_rejected(self):
        entry = package_entry(self.repo, "arm-only", "1.0.0", architecture="aarch64")
        write_index(self.repo, {"arm-only": {"versions": {"1.0.0": entry}}})
        result = self.run_pkg("-s", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg("-i", "arm-only")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no artifact for CPU architecture x86_64", result.stderr)

    def test_sync_upgrade_and_invalid_combinations(self):
        old = package_entry(self.repo, "hello", "1.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": old}}})
        result = self.run_pkg("-is", "hello", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        new = package_entry(self.repo, "hello", "2.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": old, "2.0.0": new}}})
        result = self.run_pkg("-su", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2.0.0", result.stdout)
        newer = package_entry(self.repo, "hello", "3.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": old, "2.0.0": new, "3.0.0": newer}}})
        result = self.run_pkg("-us", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("3.0.0", result.stdout)
        result = self.run_pkg("-is", "hello", "1.0.0", "extra", "--repo", str(self.repo))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires NAME", result.stderr)

    def test_short_flag_permutations_and_repeats(self):
        archive = package_entry(self.repo, "hello", "1.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": archive}}})
        for number, flags in enumerate(("-si", "-is", "-ssi", "-iis", "-sis", "-s -i", "-i -s", "hello -s -i")):
            root = Path(self.temp.name) / f"root-{number}"
            flag_args = flags.split()
            if flag_args[0] == "hello":
                command = (*flag_args, "--repo", str(self.repo))
            else:
                command = (*flag_args, "hello", "--repo", str(self.repo))
            result = self.run_pkg_at(root, *command)
            self.assertEqual(result.returncode, 0, f"{flags}: {result.stderr}")
            self.assertEqual((root / "bin/hello").read_text(), "hello 1.0.0\n")
        sync_root = Path(self.temp.name) / "sync-root"
        result = self.run_pkg_at(sync_root, "-s", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        install_root = Path(self.temp.name) / "install-root"
        result = self.run_pkg_at(install_root, "-s", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg_at(install_root, "-i", "hello")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_short_flag_conflicts_and_unknown_flags_are_deterministic(self):
        cases = (
            (("-iu",), "conflicting short flags: -iu"),
            (("-ir",), "conflicting short flags: -ir"),
            (("-siu",), "conflicting short flags: -siu"),
            (("-x",), "unknown short flag: -x"),
            (("-sxx",), "unknown short flag: -x"),
        )
        for number, (flags, message) in enumerate(cases):
            root = Path(self.temp.name) / f"invalid-root-{number}"
            result = self.run_pkg_at(root, *flags, "--repo", str(self.repo))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)
            self.assertFalse(root.exists())

    def test_package_arguments_are_validated_and_dash_names_need_separator(self):
        self.sync_package()
        result = self.run_pkg("-i", "--", "-hello")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid package name", result.stderr)
        result = self.run_pkg("-i", "hello", "not-a-version")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid version", result.stderr)
        result = self.run_pkg("-i", "-hello")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown short flag: -h", result.stderr)

    def test_parent_symlink_escape_is_rejected(self):
        self.sync_package()
        outside = Path(self.temp.name) / "outside"
        outside.mkdir()
        (self.root / "bin").symlink_to(outside, target_is_directory=True)
        result = self.run_pkg("-i", "hello")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((outside / "hello").exists())

    def test_crash_recovery_restores_backup(self):
        self.sync_package()
        self.assertEqual(self.run_pkg("-i", "hello").returncode, 0)
        state = json.loads((self.root / ".pkg/state.json").read_text())
        transaction = self.root / ".pkg/.txn-crash"
        backup = transaction / "backup"
        backup.mkdir(parents=True)
        target = self.root / "bin/hello"
        target.replace(backup / "0")
        journal = {
            "schema": 1,
            "new_state": {"schema": 1, "installed": {}, "owners": {}},
            "backups": [{"name": "0", "path": "bin/hello"}],
            "placed": [],
        }
        (transaction / "journal.json").write_text(json.dumps(journal))
        result = self.run_pkg("list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "bin/hello").read_text(), "hello 1.0.0\n")
        self.assertEqual(json.loads((self.root / ".pkg/state.json").read_text()), state)
        self.assertFalse(transaction.exists())

    def test_backup_directory_loss_fails_closed(self):
        self.sync_package()
        self.assertEqual(self.run_pkg("-i", "hello").returncode, 0)
        transaction = self.root / ".pkg/.txn-missing-backup"
        transaction.mkdir()
        (transaction / "journal.json").write_text(json.dumps({
            "schema": 1,
            "new_state": {"schema": 1, "installed": {}, "owners": {}},
            "backups": [{"name": "0", "path": "bin/hello"}],
            "placed": [],
        }))
        result = self.run_pkg("status")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("backup directory is missing", result.stderr)
        self.assertEqual((self.root / "bin/hello").read_text(), "hello 1.0.0\n")

    def test_archive_expansion_limit(self):
        archive_path = self.repo / "huge-members.tar.gz"
        with tarfile.open(archive_path, "w:gz") as archive:
            for index in range(4097):
                info = tarfile.TarInfo(f"directory-{index}")
                info.type = tarfile.DIRTYPE
                archive.addfile(info)
        entry = package_entry(self.repo, "huge", "1.0.0", archive=archive_path)
        write_index(self.repo, {"huge": {"versions": {"1.0.0": entry}}})
        self.assertEqual(self.run_pkg("sync", "--repo", str(self.repo)).returncode, 0)
        result = self.run_pkg("install", "huge")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("too many members", result.stderr)

    def test_https_downgrade_redirect_is_rejected(self):
        loader = importlib.machinery.SourceFileLoader("pkg_impl", str(PKG))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        module = importlib.util.module_from_spec(spec)
        loader.exec_module(module)
        handler = module.NoDowngradeRedirectHandler()
        request = urllib.request.Request("https://example.test/index.json")
        with self.assertRaises(module.PkgError):
            handler.redirect_request(request, object(), 302, "Found", {}, "http://example.test/index.json")


if __name__ == "__main__":
    unittest.main()
