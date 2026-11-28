#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

import fcntl
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import stat
import subprocess
import tarfile
import tempfile
import urllib.error
import urllib.request
import unittest
from pathlib import Path


PKG = Path(__file__).resolve().parents[1] / "pkg"
ARCH = "x86_64"
ABI = "linux-gnu"
LOCALES = ("C", "de_DE.UTF-8", "fr_FR.UTF-8")


def locale_env(locale):
    return {"LC_ALL": locale, "LANG": locale}


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


def load_pkg_module():
    loader = importlib.machinery.SourceFileLoader("pkg_impl", str(PKG))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


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

    def run_pkg(self, *args, env=None):
        return self.run_pkg_at(self.root, *args, env=env)

    def run_pkg_at(self, root, *args, env=None):
        environment = None
        if env is not None:
            environment = os.environ.copy()
            environment.update(env)
        return subprocess.run(
            [str(PKG), "--root", str(root), *args],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
        )

    def sync_package(self, name="hello", version="1.0.0", **kwargs):
        entry = package_entry(self.repo, name, version, **kwargs)
        write_index(self.repo, {name: {"versions": {version: entry}}})
        result = self.run_pkg("sync", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)

    def make_crash_transaction(self):
        self.sync_package()
        self.assertEqual(self.run_pkg("-i", "hello").returncode, 0)
        self.assertEqual(self.run_pkg("status").returncode, 0)
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
        return state, transaction, backup, target

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

    def test_missing_state_and_cache_initialise_across_locales(self):
        self.sync_package()
        for locale in LOCALES:
            with self.subTest(locale=locale):
                root = Path(self.temp.name) / f"missing-{locale.split('.')[0]}"
                result = self.run_pkg_at(root, "list", env=locale_env(locale))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), "")
                result = self.run_pkg_at(root, "sync", "--repo", str(self.repo), env=locale_env(locale))
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_state_file_content_is_classified_structurally(self):
        internal = self.root / ".pkg"
        internal.mkdir(parents=True, mode=0o700)
        os.chmod(internal, 0o700)
        state_file = internal / "state.json"
        valid = json.dumps({"schema": 1, "installed": {}, "owners": {}})
        cases = (
            ("empty", "", "malformed package state"),
            ("malformed", "{", "malformed package state"),
            ("invalid", json.dumps({"schema": 2, "installed": {}, "owners": {}}), "malformed package state"),
            ("valid", valid, None),
        )
        for name, content, error in cases:
            for locale in LOCALES:
                with self.subTest(case=name, locale=locale):
                    state_file.write_text(content)
                    result = self.run_pkg("list", env=locale_env(locale))
                    if error is None:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout.strip(), "")
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(error, result.stderr)

    def test_unreadable_state_file_fails_closed(self):
        internal = self.root / ".pkg"
        internal.mkdir(parents=True, mode=0o700)
        os.chmod(internal, 0o700)
        state_file = internal / "state.json"
        state_file.mkdir()
        result = self.run_pkg("list")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("package state is not a regular file", result.stderr)
        state_file.rmdir()
        state_file.write_text(json.dumps({"schema": 1, "installed": {}, "owners": {}}))
        state_file.chmod(0o000)
        if os.geteuid() != 0:
            result = self.run_pkg("list")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot read package state", result.stderr)

    def test_state_loader_does_not_match_error_text(self):
        module = load_pkg_module()
        self.root.mkdir(parents=True)
        state = module.State(self.root)
        original = module.read_file_at

        def raise_with_missing_text(*_args, **_kwargs):
            raise module.PkgError("cannot read package state: No such file or directory")

        module.read_file_at = raise_with_missing_text
        try:
            with self.assertRaises(module.PkgError):
                state.load()
            with self.assertRaises(module.PkgError):
                state.load_optional("cache.json", "package cache")
        finally:
            module.read_file_at = original

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

    def test_architecture_configuration_waits_for_lock(self):
        internal = self.root / ".pkg"
        internal.mkdir(parents=True, mode=0o700)
        os.chmod(internal, 0o700)
        config = internal / "config.json"
        process = None
        try:
            with open(internal / "lock", "a+") as held:
                fcntl.flock(held.fileno(), fcntl.LOCK_EX)
                try:
                    process = subprocess.Popen(
                        [str(PKG), "--root", str(self.root), "--arch", "aarch64", "install", "hello"],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    )
                    with self.assertRaises(subprocess.TimeoutExpired):
                        process.wait(timeout=2)
                    self.assertFalse(config.exists())
                finally:
                    fcntl.flock(held.fileno(), fcntl.LOCK_UN)
            _, stderr = process.communicate(timeout=10)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("no synced repository", stderr)
            self.assertEqual(json.loads(config.read_text())["architecture"], "aarch64")
            self.assertFalse((self.root / ".pkg/state.json").exists())
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.communicate()

    def test_install_rejects_architecture_changed_under_lock(self):
        module = load_pkg_module()
        self.sync_package()
        self.assertEqual(self.run_pkg("--arch", "x86_64", "install", "hello").returncode, 0)
        config = self.root / ".pkg/config.json"
        config.write_text(json.dumps({"schema": 1, "architecture": "aarch64"}))
        state_before = (self.root / ".pkg/state.json").read_bytes()
        state = module.State(self.root)
        with state.lock():
            with self.assertRaises(module.PkgError):
                module.install_locked(state, "hello", None, "x86_64")
        self.assertEqual(json.loads(config.read_text())["architecture"], "aarch64")
        self.assertEqual((self.root / ".pkg/state.json").read_bytes(), state_before)
        self.assertEqual((self.root / "bin/hello").read_text(), "hello 1.0.0\n")

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
        state, transaction, backup, target = self.make_crash_transaction()
        result = self.run_pkg("list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(target.read_text(), "hello 1.0.0\n")
        self.assertEqual(json.loads((self.root / ".pkg/state.json").read_text()), state)
        self.assertFalse(transaction.exists())

    def test_recovery_restore_failure_preserves_journal(self):
        state, transaction, backup, target = self.make_crash_transaction()
        result = self.run_pkg("status", env={"PKG_TEST_FAIL_RECOVERY_RESTORE": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(transaction.exists())
        self.assertTrue((transaction / "journal.json").exists())
        self.assertTrue((backup / "0").exists())
        self.assertFalse(target.exists())
        self.assertEqual(json.loads((self.root / ".pkg/state.json").read_text()), state)
        result = self.run_pkg("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(target.read_text(), "hello 1.0.0\n")
        self.assertFalse(transaction.exists())

    def test_recovery_fsync_failure_preserves_journal(self):
        state, transaction, backup, target = self.make_crash_transaction()
        result = self.run_pkg("status", env={"PKG_TEST_FAIL_RECOVERY_FSYNC": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(transaction.exists())
        self.assertTrue((transaction / "journal.json").exists())
        self.assertTrue(target.exists())
        self.assertEqual(target.read_text(), "hello 1.0.0\n")
        self.assertEqual(json.loads((self.root / ".pkg/state.json").read_text()), state)
        result = self.run_pkg("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(transaction.exists())

    def test_cleanup_fsync_failure_leaves_recoverable_tombstone(self):
        state, transaction, backup, target = self.make_crash_transaction()
        result = self.run_pkg("status", env={"PKG_TEST_FAIL_CLEANUP_FSYNC": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(transaction.exists())
        tombstones = sorted((self.root / ".pkg").glob(".pkg-cleanup-*"))
        self.assertEqual(len(tombstones), 1)
        self.assertTrue((tombstones[0] / "journal.json").exists())
        self.assertEqual(target.read_text(), "hello 1.0.0\n")
        result = self.run_pkg("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(list((self.root / ".pkg").glob(".pkg-cleanup-*")))

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

    def test_mutable_root_ancestry_is_rejected(self):
        parent = Path(self.temp.name) / "mutable-parent"
        parent.mkdir()
        parent.chmod(0o777)
        self.root = parent / "root"
        try:
            result = self.run_pkg("status")
        finally:
            parent.chmod(0o700)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mutable by an untrusted user", result.stderr)

    def test_root_ancestry_owner_and_sticky_trust_rules(self):
        module = load_pkg_module()
        effective_uid = os.geteuid()
        root = Path("/pkg/root")

        def check(parent_uid, parent_mode, expected_error=None):
            metadata = {
                Path("/"): type("Metadata", (), {"st_mode": stat.S_IFDIR | 0o755, "st_uid": 0})(),
                Path("/pkg"): type("Metadata", (), {"st_mode": stat.S_IFDIR | parent_mode, "st_uid": parent_uid})(),
                root: type("Metadata", (), {"st_mode": stat.S_IFDIR | 0o755, "st_uid": effective_uid})(),
            }
            original = module.directory_metadata
            module.directory_metadata = lambda path: metadata[Path(path)]
            try:
                if expected_error is None:
                    module.validate_root_ancestry(root)
                else:
                    with self.assertRaises(module.PkgError) as raised:
                        module.validate_root_ancestry(root)
                    self.assertIn(expected_error, str(raised.exception))
            finally:
                module.directory_metadata = original

        check(effective_uid, 0o755)
        check(effective_uid, 0o1777)
        check(0, 0o755)
        check(0, 0o1777)
        check(2000, 0o1777, "not owned by a trusted UID")
        check(2000, 0o555, "not owned by a trusted UID")

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

    def test_https_index_rejects_absolute_http_archive(self):
        module = load_pkg_module()
        archive = make_archive(self.repo, "secure", "1.0.0", {"bin/secure": "secure\n"})
        entry = package_entry(self.repo, "secure", "1.0.0", archive=archive)
        entry["artifacts"][ARCH]["archive"] = "http://mirror.test/secure.tar.gz"
        index = {
            "schema": 1,
            "generated_at": "2026-01-01T00:00:00Z",
            "repository": {"name": "test", "channel": "stable", "architectures": ["x86_64"]},
            "packages": {"secure": {"versions": {"1.0.0": entry}}},
        }
        with self.assertRaises(module.PkgError):
            module.validate_index(index, "remote", "https")

    def test_non_http_redirects_and_ftp_transport_are_rejected(self):
        module = load_pkg_module()
        handler = module.NoDowngradeRedirectHandler()
        request = urllib.request.Request("https://example.test/index.json")
        for target in ("http://example.test/index.json", "ftp://example.test/index.json", "file:///tmp/index.json", "gopher://example.test/index"):
            with self.assertRaises(module.PkgError):
                handler.redirect_request(request, object(), 302, "Found", {}, target)
        with self.assertRaises(urllib.error.URLError):
            module.open_url(urllib.request.Request("ftp://example.test/index.json"), 1)

    def test_standalone_upgrade_uses_cached_repository(self):
        old = package_entry(self.repo, "hello", "1.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": old}}})
        result = self.run_pkg("-is", "hello", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        new = package_entry(self.repo, "hello", "2.0.0")
        write_index(self.repo, {"hello": {"versions": {"1.0.0": old, "2.0.0": new}}})
        result = self.run_pkg("sync", "--repo", str(self.repo))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_pkg("-u")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2.0.0", result.stdout)
        result = self.run_pkg("upgrade")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_python_parser_upgrade_matches_stacked_flags(self):
        module = load_pkg_module()
        values, operation, operands = module.parse_python_cli(["-u"])
        self.assertEqual(operation, "upgrade")
        self.assertIsNone(values["repo"])
        self.assertEqual(operands, [])
        values, operation, operands = module.parse_python_cli(["-us", "--repo", "repo"])
        self.assertEqual(operation, "sync-upgrade")
        self.assertEqual(values["repo"], "repo")
        self.assertEqual(operands, [])


if __name__ == "__main__":
    unittest.main()
