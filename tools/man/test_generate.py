import json
import os
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import unittest
import warnings
from pathlib import Path
from unittest.mock import patch

TOOLS_MAN = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_MAN.parents[1]
ABI_HEADER = REPO_ROOT / "kernel/include/astia/api/syscalls.h"
LIBC_ROOT = REPO_ROOT / "libc"
sys.path.insert(0, str(TOOLS_MAN))

import generate


class GenerateTest(unittest.TestCase):
    def test_public_abi_prototypes_are_c_syntax(self):
        prototypes = generate.public_abi_prototypes(ABI_HEADER)

        self.assertEqual(
            prototypes["sigwait"],
            "long astia_sigwait(const uint32_t* set, int32_t* signal);",
        )
        self.assertEqual(prototypes["alarm"], "long astia_alarm(uint32_t seconds);")
        self.assertEqual(
            prototypes["io_submit"],
            "long astia_io_submit(astia_rings* ring, uint32_t to_submit);",
        )
        for prototype in prototypes.values():
            self.assertTrue(prototype.endswith(";"))

    def test_public_user_synopses_cover_all_canonical_ids(self):
        canonical = generate.syscall_abi(ABI_HEADER)
        synopses = generate.public_user_synopses(ABI_HEADER, LIBC_ROOT)
        missing = [name for _, name in canonical if name not in synopses]

        self.assertEqual(missing, ["sigreturn"])
        self.assertEqual(
            synopses["io_setup"],
            "long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);",
        )
        self.assertEqual(
            synopses["unmount"], "long astia_unmount(const char* target);"
        )
        self.assertEqual(
            synopses["clock_gettime"],
            "long astia_clock_gettime(uint32_t clock_id, astia_timespec* out);",
        )
        self.assertEqual(
            synopses["clock_getres"],
            "long astia_clock_getres(uint32_t clock_id, astia_timespec* out);",
        )
        self.assertEqual(
            synopses["execve"],
            "int execve(const char* path, char* const argv[], char* const envp[]);",
        )
        self.assertEqual(
            synopses["sigsuspend"], "int sigsuspend(const sigset_t* mask);"
        )
        self.assertEqual(
            synopses["waitpid"],
            "long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);",
        )
        self.assertEqual(generate.SOURCE_OVERRIDES["execve"], "libc/src/unistd.c")
        self.assertEqual(
            generate.SOURCE_OVERRIDES["waitpid"],
            "kernel/include/astia/api/syscalls.h",
        )
        self.assertEqual(generate.CATEGORY_OVERRIDES["execve"], "proc/process")
        self.assertEqual(generate.CATEGORY_OVERRIDES["sigsuspend"], "proc/signal")
        self.assertEqual(generate.CATEGORY_OVERRIDES["waitpid"], "proc/process")
        self.assertEqual(generate.SOURCE_OVERRIDES["sigsuspend"], "libc/src/signal.c")

    def test_public_body_overrides_rewrite_execve_semantics(self):
        sections = generate.body_sections(generate.PUBLIC_BODY_OVERRIDES["execve"])
        returns = "\n".join(sections["RETURN VALUE"])
        errors = "\n".join(sections["ERRORS"])
        notes = "\n".join(sections["NOTES"])

        self.assertIn("-1 and sets errno", returns)
        self.assertNotIn("negative errno", returns)
        self.assertTrue(any(line.strip().startswith("E2BIG:") for line in sections["ERRORS"]))
        self.assertNotIn("-E2BIG", errors)
        self.assertIn("libc/src/unistd.c", notes)
        self.assertIn("FD_CLOEXEC", "\n".join(sections["DESCRIPTION"]))
        self.assertIn("argv or envp", errors)
        self.assertNotIn("A path, vector, or string pointer is invalid", errors)
        self.assertIn("null path returns EINVAL", errors)
        self.assertIn("not a valid ELF executable image", errors)
        self.assertIn("ENAMETOOLONG", errors)
        self.assertIn("resolved path", errors)
        self.assertIn("ENOEXEC: The file is empty", errors)
        self.assertNotIn("not a supported executable image", errors)
        self.assertIn("no current process or thread context", errors)
        self.assertIn("The current process or thread could not be retained", errors)
        self.assertNotIn("ESRCH: There is no current process", errors)
        self.assertIn("resolved against the caller's working directory by libc", notes)
        self.assertIn("execve.cpp", notes)
        self.assertIn("exec_loader.cpp", notes)
        self.assertEqual(generate.error_entries("E2BIG: Too big.")[0][0], "E2BIG")

    def test_waitpid_documents_interruption_and_resource_exhaustion(self):
        body = (
            "Synopsis:\n"
            "    long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);\n\n"
            "Description:\n    Waits for a child.\n\n"
            "Return Value:\n    Negative errno.\n\n"
            "Errors:\n    Old errors.\n\n"
            "Notes:\n    Old notes."
        )
        overrides = generate.PUBLIC_SECTION_OVERRIDES["waitpid"]
        for heading, content in overrides.items():
            body = generate.replace_section(body, heading, content)
        sections = generate.body_sections(body)
        errors = "\n".join(sections["ERRORS"])
        notes = "\n".join(sections["NOTES"])

        self.assertIn("astia_waitpid_options", notes)
        self.assertIn("translates that result to -1", notes)
        self.assertIn("unblocked interrupting signal", notes)
        self.assertIn("-EINTR", errors)
        self.assertIn("process waiter table", errors)
        self.assertIn("-EAGAIN", errors)
        self.assertIn("without a wake slot", notes)
        self.assertNotIn("-ESRCH", notes)

    def test_sigsuspend_uses_libc_contract(self):
        sections = generate.body_sections(generate.PUBLIC_BODY_OVERRIDES["sigsuspend"])
        returns = "\n".join(sections["RETURN VALUE"])
        errors = "\n".join(sections["ERRORS"])
        notes = "\n".join(sections["NOTES"])

        self.assertIn("Never returns 0", returns)
        self.assertIn("errno to EINTR", returns)
        self.assertIn("-1", returns)
        self.assertIn("EINTR", errors)
        self.assertIn("EAGAIN", errors)
        self.assertIn("EFAULT", errors)
        self.assertNotIn("-EINTR", errors)
        self.assertIn("libc/src/signal.c", notes)
        self.assertIn("sigsuspend.cpp", notes)
        self.assertEqual(generate.SOURCE_OVERRIDES["sigsuspend"], "libc/src/signal.c")

    def test_clock_errors_include_missing_timer_source(self):
        time_errors = generate.PUBLIC_SECTION_OVERRIDES["clock_gettime"]["ERRORS"]
        res_errors = generate.PUBLIC_SECTION_OVERRIDES["clock_getres"]["ERRORS"]

        self.assertIn("-EIO", time_errors)
        self.assertIn("timer source", time_errors)
        self.assertIn("-EIO", res_errors)
        self.assertIn("timer source", res_errors)

    def test_fallback_body_uses_public_synopsis_and_missing_detail_label(self):
        body = generate.fallback_body(
            "sigwait",
            "long astia_sigwait(const uint32_t* set, int32_t* signal);",
        )

        self.assertIn(
            "Synopsis:\n    long astia_sigwait(const uint32_t* set, int32_t* signal);",
            body,
        )
        self.assertIn("System call handler for `sigwait`", body)
        self.assertIn("Detailed manual text has not been added", body)
        self.assertNotIn("u64 set_ptr", body)

    def _export_pages(self, count: int = 70, version: str = "old"):
        canonical = [(index, f"fake_{index}") for index in range(count)]
        pages = {
            (name, 2): (
                generate.fallback_body(name, f"long {name}_{version}(void);"),
                "proc",
            )
            for _, name in canonical
        }
        return canonical, pages, {}

    def test_atomic_output_replaces_symlinks_without_touching_targets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            output.mkdir()
            outside.mkdir()
            sentinel = outside / "sentinel.txt"
            sentinel.write_text("sentinel")
            target = outside / "dangling.txt"

            with generate.OutputDirectory.open(output, root) as directory:
                for name in ("nodivia.css", "io_setup.2.html", "index.html"):
                    with self.subTest(name=name):
                        output_file = output / name
                        if output_file.exists() or output_file.is_symlink():
                            output_file.unlink()
                        output_file.symlink_to(sentinel)
                        directory.write_atomic(name, "generated")
                        self.assertEqual(sentinel.read_text(), "sentinel")
                        self.assertFalse(output_file.is_symlink())
                        self.assertEqual(output_file.read_text(), "generated")

                css = output / "nodivia.css"
                if css.exists() or css.is_symlink():
                    css.unlink()
                css.symlink_to(target)
                directory.write_atomic("nodivia.css", "generated")
                self.assertFalse(target.exists())
                self.assertFalse(css.is_symlink())
                self.assertEqual(css.read_text(), "generated")

                for name in ("../escape.html", "..", "."):
                    with self.subTest(name=name):
                        with self.assertRaises(ValueError):
                            directory.write_atomic(name, "escape")
                self.assertFalse((root / "escape.html").exists())

    def test_output_directory_rejects_symlink_components_and_escapes(self):
        with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as external:
            root = Path(temporary)
            outside = Path(external)
            (root / ".cache").symlink_to(outside)
            linked = root / "linked"
            linked.symlink_to(outside)
            dangling = root / "dangling"
            dangling.symlink_to(root / "missing")

            with self.assertRaises(ValueError):
                generate.OutputDirectory.open(root / ".cache/man7", root)
            with self.assertRaises(ValueError):
                generate.OutputDirectory.open(linked, root)
            with self.assertRaises(ValueError):
                generate.OutputDirectory.open(dangling, root)
            with self.assertRaises(ValueError):
                generate.OutputDirectory.open(outside / "man7", root)
            with self.assertRaises(ValueError):
                generate.export_section2(
                    root / ".cache/section2",
                    [],
                    {},
                    {},
                    REPO_ROOT / "kernel/src",
                    ABI_HEADER,
                    trusted_root=root,
                )

    def test_pinned_output_directory_does_not_follow_a_swapped_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            output.mkdir()
            outside.mkdir()

            with generate.OutputDirectory.open(output, root) as directory:
                original = root / "man7-original"
                output.rename(original)
                output.symlink_to(outside)
                directory.write_atomic("nodivia.css", "pinned")

            self.assertEqual((original / "nodivia.css").read_text(), "pinned")
            self.assertFalse((outside / "nodivia.css").exists())

    def test_man_transaction_preserves_unrelated_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                transaction.output.write_atomic("nodivia.css", "old css")
                transaction.output.write_atomic("index.html", "old index")
                transaction.output.write_atomic("old.2.html", "old page")
                transaction.output.write_atomic("keep.2.html", "old keep")
                generate.write_man_manifest(
                    transaction.output,
                    {"nodivia.css", "index.html", "old.2.html", "keep.2.html"},
                )
                transaction.commit()

            (output / "custom.2.html").write_text("custom")
            (output / "architecture-custom.html").write_text("custom")
            (output / "pl").mkdir()
            (output / "pl" / "my-notes.7.html").write_text("notes")
            snapshot = {path.name: path.read_bytes() for path in output.iterdir() if path.is_file()}

            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                transaction.output.write_atomic("nodivia.css", "new css")
                transaction.output.write_atomic("index.html", "new index")
                transaction.output.write_atomic("keep.2.html", "new keep")
                generate.write_man_manifest(
                    transaction.output,
                    {"nodivia.css", "index.html", "keep.2.html"},
                )
                transaction.commit()

            self.assertEqual((output / "nodivia.css").read_text(), "new css")
            self.assertEqual((output / "index.html").read_text(), "new index")
            self.assertEqual((output / "keep.2.html").read_text(), "new keep")
            self.assertFalse((output / "old.2.html").exists())
            self.assertEqual((output / "custom.2.html").read_text(), "custom")
            self.assertEqual((output / "architecture-custom.html").read_text(), "custom")
            self.assertEqual((output / "pl" / "my-notes.7.html").read_text(), "notes")
            self.assertEqual(
                json.loads((output / generate.MAN7_MANIFEST).read_text()),
                {"files": ["index.html", "keep.2.html", "nodivia.css"]},
            )

    def test_man_transaction_does_not_traverse_symlink_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            outside.mkdir()
            (outside / "old.7.html").write_text("outside old")
            (outside / "my-notes.7.html").write_text("outside notes")
            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                transaction.output.write_atomic("index.html", "old index")
                generate.write_man_manifest(
                    transaction.output,
                    {"index.html", "pl/old.7.html", "pl/my-notes.7.html"},
                )
                transaction.commit()
            (output / "pl").symlink_to(outside)

            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                transaction.output.write_atomic("index.html", "new index")
                generate.write_man_manifest(transaction.output, {"index.html"})
                transaction.commit()

            self.assertTrue((output / "pl").is_symlink())
            self.assertEqual((outside / "old.7.html").read_text(), "outside old")
            self.assertEqual((outside / "my-notes.7.html").read_text(), "outside notes")

    def test_man_manifest_ignores_missing_malformed_and_unsafe_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            output.mkdir()
            outside.mkdir()
            sentinel = outside / "sentinel.json"
            sentinel.write_text("sentinel")

            with generate.OutputDirectory.open(output, root) as directory:
                self.assertEqual(generate.read_owned_files(directory), set())
                directory.write_atomic(generate.MAN7_MANIFEST, "{")
                self.assertEqual(generate.read_owned_files(directory), set())
                directory.write_atomic(
                    generate.MAN7_MANIFEST,
                    json.dumps(
                        {
                            "files": [
                                None,
                                5,
                                "../outside/escape.html",
                                "/absolute.html",
                                "..",
                                "safe.2.html",
                            ]
                        }
                    ),
                )
                self.assertEqual(generate.read_owned_files(directory), {"safe.2.html"})

                (output / generate.MAN7_MANIFEST).unlink()
                (output / generate.MAN7_MANIFEST).symlink_to(sentinel)
                with self.assertRaises(ValueError):
                    generate.read_owned_files(directory)
                generate.write_man_manifest(directory, {"safe.2.html"})

            self.assertEqual(sentinel.read_text(), "sentinel")
            self.assertFalse((output / generate.MAN7_MANIFEST).is_symlink())
            self.assertEqual(
                json.loads((output / generate.MAN7_MANIFEST).read_text()),
                {"files": ["safe.2.html"]},
            )

    def test_transaction_rejects_active_directory_swap(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            outside.mkdir()
            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                transaction.output.write_atomic("index.html", "old index")
                generate.write_man_manifest(transaction.output, {"index.html"})
                transaction.commit()
            before = (output / "index.html").read_text()

            with self.assertRaises(RuntimeError):
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    backup = root / "man7-original"
                    output.rename(backup)
                    output.symlink_to(outside)
                    transaction.commit()

            self.assertEqual((root / "man7-original" / "index.html").read_text(), before)
            self.assertTrue(output.is_symlink())
            self.assertFalse((outside / "index.html").exists())
            self.assertEqual(
                [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                [],
            )

    def test_manifest_reader_rejects_non_regular_entries_without_blocking(self):
        def timeout(_signum, _frame):
            raise TimeoutError("manifest read blocked")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            output.mkdir()
            outside.mkdir()
            sentinel = outside / "sentinel.json"
            sentinel.write_text("sentinel")

            with generate.OutputDirectory.open(output, root) as directory:
                self.assertIsNone(directory.read_manifest("manifest.json"))
                manifest = output / "manifest.json"
                manifest.symlink_to(sentinel)
                with self.assertRaises(ValueError):
                    directory.read_manifest("manifest.json")

                manifest.unlink()
                manifest.mkdir()
                with self.assertRaises(ValueError):
                    directory.read_manifest("manifest.json")
                manifest.rmdir()

                os.mkfifo(manifest)
                previous_handler = signal.signal(signal.SIGALRM, timeout)
                signal.setitimer(signal.ITIMER_REAL, 1)
                try:
                    with self.assertRaises(ValueError):
                        directory.read_manifest("manifest.json")
                finally:
                    signal.setitimer(signal.ITIMER_REAL, 0)
                    signal.signal(signal.SIGALRM, previous_handler)
                manifest.unlink()

                device = os.open("/dev/null", os.O_RDONLY)
                with patch.object(generate.os, "open", return_value=device):
                    with self.assertRaises(ValueError):
                        directory.read_manifest("manifest.json")

            self.assertEqual(sentinel.read_text(), "sentinel")

    def _old_man_generation(self, root: Path) -> Path:
        output = root / "man7"
        with generate.TransactionalDirectory.begin(
            output, root, generate.MAN7_MANIFEST, generate.read_owned_files
        ) as transaction:
            transaction.output.write_atomic("index.html", "old index")
            generate.write_man_manifest(transaction.output, {"index.html"})
            transaction.commit()
        return output

    def test_transaction_aborts_if_initial_snapshot_vanishes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            (output / "custom.2.html").write_text("custom")
            before = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }

            with patch.object(
                generate,
                "directory_snapshot",
                side_effect=FileNotFoundError("snapshot race"),
            ):
                with self.assertRaises(RuntimeError):
                    generate.TransactionalDirectory.begin(
                        output,
                        root,
                        generate.MAN7_MANIFEST,
                        generate.read_owned_files,
                    )

            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                before,
            )
            self.assertEqual(
                [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                [],
            )

    def test_transaction_aborts_if_commit_snapshot_vanishes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            (output / "custom.2.html").write_text("custom")
            before = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }
            original_snapshot = generate.directory_snapshot
            calls = 0

            def flaky_snapshot(directory, prefix=""):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise FileNotFoundError("snapshot race")
                return original_snapshot(directory, prefix)

            with patch.object(
                generate, "directory_snapshot", side_effect=flaky_snapshot
            ):
                with self.assertRaises(RuntimeError):
                    with generate.TransactionalDirectory.begin(
                        output,
                        root,
                        generate.MAN7_MANIFEST,
                        generate.read_owned_files,
                    ) as transaction:
                        transaction.output.write_atomic("index.html", "new index")
                        generate.write_man_manifest(
                            transaction.output, {"index.html"}
                        )
                        transaction.commit()

            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                before,
            )
            self.assertEqual(
                [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                [],
            )

    def test_copy_closes_source_descriptors_when_destination_open_fails(self):
        fd_root = Path("/proc/self/fd")
        if not fd_root.is_dir():
            self.skipTest("procfs is unavailable")
        cases = ("directory", "file")
        for kind in cases:
            with self.subTest(kind=kind):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    source = root / "source"
                    destination = root / "destination"
                    source.mkdir()
                    destination.mkdir()
                    if kind == "directory":
                        (source / "sub").mkdir()
                        (source / "sub" / "payload").write_text("payload")
                    else:
                        (source / "payload.bin").write_text("payload")
                    source_fd = os.open(
                        source, os.O_RDONLY | os.O_DIRECTORY
                    )
                    destination_fd = os.open(
                        destination, os.O_RDONLY | os.O_DIRECTORY
                    )
                    before = len(list(fd_root.iterdir()))
                    real_open = generate.os.open

                    def fail_open(path, flags, mode=0o777, *, dir_fd=None):
                        target = "sub" if kind == "directory" else "payload.bin"
                        if (
                            path == target
                            and dir_fd == destination_fd
                            and (
                                flags & os.O_DIRECTORY
                                if kind == "directory"
                                else flags & os.O_WRONLY
                            )
                        ):
                            raise OSError("destination open failure")
                        return real_open(path, flags, mode, dir_fd=dir_fd)

                    try:
                        with patch.object(
                            generate.os, "open", side_effect=fail_open
                        ), patch.object(
                            generate.os, "link", side_effect=OSError("no link")
                        ):
                            with self.assertRaises(OSError):
                                generate.copy_unowned_entries(
                                    source_fd,
                                    destination_fd,
                                    set(),
                                    "manifest.json",
                                )
                        self.assertEqual(len(list(fd_root.iterdir())), before)
                    finally:
                        os.close(source_fd)
                        os.close(destination_fd)

    def test_transaction_rejects_special_manifest_entries(self):
        cases = ("symlink", "directory", "fifo", "socket")
        for kind in cases:
            with self.subTest(kind=kind):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output = self._old_man_generation(root)
                    outside = root / "outside"
                    outside.mkdir()
                    sentinel = outside / "sentinel.json"
                    sentinel.write_text("sentinel")
                    manifest = output / generate.MAN7_MANIFEST
                    manifest.unlink()
                    connection = None
                    if kind == "symlink":
                        manifest.symlink_to(sentinel)
                    elif kind == "directory":
                        manifest.mkdir()
                    elif kind == "fifo":
                        os.mkfifo(manifest)
                    else:
                        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        connection.bind(str(manifest))
                    try:
                        with self.assertRaises(ValueError):
                            generate.TransactionalDirectory.begin(
                                output,
                                root,
                                generate.MAN7_MANIFEST,
                                generate.read_owned_files,
                            )
                        self.assertEqual((output / "index.html").read_text(), "old index")
                        self.assertEqual(sentinel.read_text(), "sentinel")
                        self.assertTrue((output / generate.MAN7_MANIFEST).exists())
                        self.assertEqual(
                            [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                            [],
                        )
                    finally:
                        if connection is not None:
                            connection.close()

    def test_transaction_rejects_unpreservable_unowned_entries(self):
        cases = ("socket", "device")
        for kind in cases:
            with self.subTest(kind=kind):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output = self._old_man_generation(root)
                    connection = None
                    if kind == "socket":
                        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        connection.bind(str(output / "entry"))
                        context = None
                    else:
                        (output / "entry").write_text("device placeholder")
                        original_stat = generate.os.stat

                        def fake_stat(path, *, dir_fd=None, follow_symlinks=True):
                            if path == "entry":
                                return os.stat_result(
                                    (stat.S_IFCHR | 0o600, 0, 0, 0, 0, 0, 0, 0, 0, 0)
                                )
                            return original_stat(
                                path, dir_fd=dir_fd, follow_symlinks=follow_symlinks
                            )

                        context = patch.object(generate.os, "stat", side_effect=fake_stat)
                    try:
                        if context is not None:
                            with context, patch.object(
                                generate.os, "link", side_effect=OSError("no device link")
                            ):
                                with self.assertRaises(ValueError):
                                    generate.TransactionalDirectory.begin(
                                        output,
                                        root,
                                        generate.MAN7_MANIFEST,
                                        generate.read_owned_files,
                                    )
                        else:
                            with self.assertRaises(ValueError):
                                generate.TransactionalDirectory.begin(
                                    output,
                                    root,
                                    generate.MAN7_MANIFEST,
                                    generate.read_owned_files,
                                )
                        self.assertEqual((output / "index.html").read_text(), "old index")
                        self.assertTrue((output / "entry").exists())
                        self.assertEqual(
                            [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                            [],
                        )
                    finally:
                        if connection is not None:
                            connection.close()

    def test_transaction_preserves_directory_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            private = output / "private"
            private.mkdir()
            previous_umask = os.umask(0o022)
            try:
                os.chmod(output, 0o700)
                os.chmod(private, 0o750)
                output_info = output.stat()
                private_info = private.stat()
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    transaction.commit()
                result_info = output.stat()
                result_private = (output / "private").stat()
                self.assertEqual(stat.S_IMODE(result_info.st_mode), 0o700)
                self.assertEqual(stat.S_IMODE(result_private.st_mode), 0o750)
                self.assertEqual(
                    (result_info.st_uid, result_info.st_gid),
                    (output_info.st_uid, output_info.st_gid),
                )
                self.assertEqual(
                    (result_private.st_uid, result_private.st_gid),
                    (private_info.st_uid, private_info.st_gid),
                )
            finally:
                os.umask(previous_umask)

    def test_copy_fallback_handles_short_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            payload = bytes(range(256)) * 4
            payload_path = output / "payload.bin"
            payload_path.write_bytes(payload)
            os.chmod(payload_path, 0o640)
            payload_info = payload_path.stat()
            writes = []
            original_write = generate.os.write

            def short_write(descriptor, data):
                writes.append(len(data))
                return original_write(descriptor, data[:3])

            with patch.object(generate.os, "link", side_effect=OSError("no link")):
                with patch.object(generate.os, "write", side_effect=short_write):
                    with generate.TransactionalDirectory.begin(
                        output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                    ) as transaction:
                        transaction.output.write_atomic("index.html", "new index")
                        generate.write_man_manifest(transaction.output, {"index.html"})
                        transaction.commit()

            self.assertGreater(len(writes), 1)
            copied = output / "payload.bin"
            self.assertEqual(copied.read_bytes(), payload)
            copied_info = copied.stat()
            self.assertEqual(stat.S_IMODE(copied_info.st_mode), 0o640)
            self.assertEqual(
                (copied_info.st_uid, copied_info.st_gid),
                (payload_info.st_uid, payload_info.st_gid),
            )
            self.assertEqual((output / "index.html").read_text(), "new index")

    def test_transaction_aborts_when_active_content_changes(self):
        cases = ("added", "replaced")
        for kind in cases:
            with self.subTest(kind=kind):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output = self._old_man_generation(root)
                    with self.assertRaises(RuntimeError):
                        with generate.TransactionalDirectory.begin(
                            output,
                            root,
                            generate.MAN7_MANIFEST,
                            generate.read_owned_files,
                        ) as transaction:
                            transaction.output.write_atomic("index.html", "new index")
                            generate.write_man_manifest(transaction.output, {"index.html"})
                            if kind == "added":
                                (output / "late.txt").write_text("late")
                            else:
                                (output / "index.html").write_text("externally changed")
                            transaction.commit()
                    if kind == "added":
                        self.assertEqual((output / "late.txt").read_text(), "late")
                        self.assertEqual((output / "index.html").read_text(), "old index")
                    else:
                        self.assertEqual(
                            (output / "index.html").read_text(), "externally changed"
                        )
                    self.assertEqual(
                        [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                        [],
                    )

    def test_transaction_reports_post_activation_cleanup_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter("always")
                with patch.object(
                    generate,
                    "remove_tree",
                    side_effect=OSError("deferred cleanup"),
                ):
                    with generate.TransactionalDirectory.begin(
                        output,
                        root,
                        generate.MAN7_MANIFEST,
                        generate.read_owned_files,
                    ) as transaction:
                        transaction.output.write_atomic("index.html", "new index")
                        generate.write_man_manifest(
                            transaction.output, {"index.html"}
                        )
                        transaction.commit()
            self.assertEqual((output / "index.html").read_text(), "new index")
            retained = [
                path for path in root.iterdir() if path.name.startswith(".astia-stage-")
            ]
            self.assertEqual(len(retained), 1)
            messages = [str(item.message) for item in caught]
            self.assertTrue(
                any(
                    "deferred cleanup" in message
                    and retained[0].name in message
                    and "deferred cleanup" in message
                    for message in messages
                )
            )
            shutil.rmtree(retained[0])

    def test_transaction_closes_directory_descriptors(self):
        fd_root = Path("/proc/self/fd")
        if not fd_root.is_dir():
            self.skipTest("procfs is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            before = len(list(fd_root.iterdir()))
            for _ in range(3):
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    transaction.commit()
            manifest = output / generate.MAN7_MANIFEST
            manifest.unlink()
            manifest.symlink_to(root / "outside-fd")
            with self.assertRaises(ValueError):
                generate.TransactionalDirectory.begin(
                    output,
                    root,
                    generate.MAN7_MANIFEST,
                    generate.read_owned_files,
                )
            after = len(list(fd_root.iterdir()))
            self.assertEqual(after, before)

    def test_transaction_aborts_on_entry_injected_before_activation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            before = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }

            def inject_entry():
                (output / "late.txt").write_text("late")

            with self.assertRaises(RuntimeError):
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    transaction.commit(before_activation=inject_entry)

            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file() and path.name != "late.txt"
                },
                before,
            )
            self.assertEqual((output / "late.txt").read_text(), "late")
            self.assertEqual(
                [
                    path
                    for path in root.iterdir()
                    if path.name.startswith(".astia-stage-")
                    or path.name.startswith(".astia-retired-")
                ],
                [],
            )

    def test_copy_preserves_xattrs_for_fallback_and_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            payload = output / "payload.bin"
            payload.write_bytes(b"payload")
            os.setxattr(payload, "user.astia", b"file-xattr")
            private = output / "private"
            private.mkdir()
            os.setxattr(private, "user.astia", b"dir-xattr")
            os.setxattr(output, "user.astia", b"root-xattr")
            with patch.object(generate.os, "link", side_effect=OSError("no link")):
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    transaction.commit()

            self.assertEqual(
                os.getxattr(output / "payload.bin", "user.astia"), b"file-xattr"
            )
            self.assertEqual(
                os.getxattr(output / "private", "user.astia"), b"dir-xattr"
            )
            self.assertEqual(os.getxattr(output, "user.astia"), b"root-xattr")
    def test_transaction_aborts_if_xattr_changes_at_activation_boundary(self):
        for kind in ("directory", "root"):
            with self.subTest(kind=kind):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output = self._old_man_generation(root)
                    watched = output / "watched"
                    watched.mkdir()
                    os.setxattr(watched, "user.astia", b"old")
                    os.setxattr(output, "user.astia", b"old")
                    before = {
                        path.name: path.read_bytes()
                        for path in output.iterdir()
                        if path.is_file()
                    }

                    def mutate():
                        target = watched if kind == "directory" else output
                        os.setxattr(target, "user.astia", b"new")

                    with self.assertRaises(RuntimeError):
                        with generate.TransactionalDirectory.begin(
                            output,
                            root,
                            generate.MAN7_MANIFEST,
                            generate.read_owned_files,
                        ) as transaction:
                            transaction.output.write_atomic("index.html", "new index")
                            generate.write_man_manifest(
                                transaction.output, {"index.html"}
                            )
                            transaction.commit(before_activation=mutate)

                    self.assertEqual(
                        {
                            path.name: path.read_bytes()
                            for path in output.iterdir()
                            if path.is_file()
                        },
                        before,
                    )
                    self.assertEqual(
                        os.getxattr(
                            watched if kind == "directory" else output,
                            "user.astia",
                        ),
                        b"new",
                    )
                    self.assertEqual(
                        [
                            path
                            for path in root.iterdir()
                            if path.name.startswith(".astia-stage-")
                            or path.name.startswith(".astia-retired-")
                        ],
                        [],
                    )

    def test_replace_xattrs_removes_destination_only_user_xattr(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            destination = root / "destination"
            source.mkdir()
            destination.mkdir()
            os.setxattr(destination, "user.inherited", b"inherited")
            source_fd = os.open(source, os.O_RDONLY | os.O_DIRECTORY)
            destination_fd = os.open(destination, os.O_RDONLY | os.O_DIRECTORY)
            try:
                generate.replace_xattrs(source_fd, destination_fd, None, ".")
            finally:
                os.close(source_fd)
                os.close(destination_fd)
            self.assertEqual(os.listxattr(destination), [])

    def test_copy_preserves_posix_acl(self):
        setfacl = shutil.which("setfacl")
        if setfacl is None:
            self.skipTest("setfacl is unavailable; ACL integration test skipped")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            payload = output / "payload.bin"
            payload.write_bytes(b"payload")
            result = subprocess.run(
                [setfacl, "-m", "u:root:r", str(payload)],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                self.skipTest(
                    f"setfacl failed; ACL integration test skipped: {result.stderr.strip()}"
                )
            acl_value = os.getxattr(payload, "system.posix_acl_access")

            with patch.object(generate.os, "link", side_effect=OSError("no link")):
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    transaction.commit()

            self.assertEqual(
                os.getxattr(output / "payload.bin", "system.posix_acl_access"),
                acl_value,
            )

    def test_copy_removes_inherited_default_acl_when_absent_from_source(self):
        setfacl = shutil.which("setfacl")
        if setfacl is None:
            self.skipTest("setfacl is unavailable; default ACL test skipped")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            plain = output / "plain"
            plain.mkdir()
            result = subprocess.run(
                [setfacl, "-d", "-m", "u:root:r", str(output)],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                self.skipTest(
                    f"setfacl failed; default ACL test skipped: {result.stderr.strip()}"
                )
            self.assertFalse(
                "system.posix_acl_default" in os.listxattr(plain),
                "fixture plain directory must predate the root default ACL",
            )

            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                transaction.output.write_atomic("index.html", "new index")
                generate.write_man_manifest(transaction.output, {"index.html"})
                transaction.commit()

            attributes = os.listxattr(output / "plain")
            self.assertNotIn("system.posix_acl_default", attributes)
            self.assertNotIn("system.posix_acl_access", attributes)

    def test_transaction_requires_proc_fd(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            before = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }
            with patch.object(generate, "PROC_FD_ROOT", root / "missing-proc"):
                with self.assertRaises(ValueError):
                    generate.TransactionalDirectory.begin(
                        output,
                        root,
                        generate.MAN7_MANIFEST,
                        generate.read_owned_files,
                    )
            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                before,
            )

    def test_transaction_aborts_if_xattr_cannot_be_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            payload = output / "payload.bin"
            payload.write_bytes(b"payload")
            os.setxattr(payload, "user.astia", b"file-xattr")
            original = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }
            real_setxattr = generate.os.setxattr

            def fail_setxattr(path, attribute, value, flags=0, *, follow_symlinks=True):
                if attribute == "user.astia":
                    raise OSError("injected xattr failure")
                return real_setxattr(
                    path,
                    attribute,
                    value,
                    flags=flags,
                    follow_symlinks=follow_symlinks,
                )

            with patch.object(generate.os, "link", side_effect=OSError("no link")):
                with patch.object(generate.os, "setxattr", side_effect=fail_setxattr):
                    with self.assertRaises(ValueError):
                        with generate.TransactionalDirectory.begin(
                            output,
                            root,
                            generate.MAN7_MANIFEST,
                            generate.read_owned_files,
                        ) as transaction:
                            transaction.output.write_atomic(
                                "index.html", "new index"
                            )
                            generate.write_man_manifest(
                                transaction.output, {"index.html"}
                            )
                            transaction.commit()

            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                original,
            )
            self.assertEqual(
                os.getxattr(output / "payload.bin", "user.astia"), b"file-xattr"
            )
            self.assertEqual(
                [
                    path
                    for path in root.iterdir()
                    if path.name.startswith(".astia-stage-")
                    or path.name.startswith(".astia-retired-")
                ],
                [],
            )

    def test_transaction_preserves_retired_tree_when_mutation_races_exchange(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            (output / "old-unrelated").write_text("old unrelated")
            original_exchange = generate.exchange_directories

            def mutate_then_exchange(parent, first, second):
                (output / "late.txt").write_text("late")
                return original_exchange(parent, first, second)

            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter("always")
                with patch.object(
                    generate,
                    "exchange_directories",
                    side_effect=mutate_then_exchange,
                ):
                    with generate.TransactionalDirectory.begin(
                        output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                    ) as transaction:
                        transaction.output.write_atomic("index.html", "new index")
                        generate.write_man_manifest(
                            transaction.output, {"index.html"}
                        )
                        transaction.commit()

            self.assertEqual((output / "index.html").read_text(), "new index")
            self.assertFalse((output / "late.txt").exists())
            self.assertEqual((output / "old-unrelated").read_text(), "old unrelated")
            retired = [
                path
                for path in root.iterdir()
                if path.name.startswith(".astia-retired-")
                or path.name.startswith(".astia-stage-")
            ]
            self.assertEqual(len(retired), 1)
            self.assertEqual((retired[0] / "index.html").read_text(), "old index")
            self.assertEqual((retired[0] / "late.txt").read_text(), "late")
            self.assertEqual(
                (retired[0] / "old-unrelated").read_text(), "old unrelated"
            )
            self.assertTrue(
                any(
                    "retired generation retained" in str(item.message)
                    and retired[0].name in str(item.message)
                    for item in caught
                )
            )
            shutil.rmtree(retired[0])

    def test_export_cleanup_uses_manifest_ownership(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            export_dir = root / "section2"
            outside = root / "outside"
            export_dir.mkdir()
            outside.mkdir()
            sentinel = outside / "sentinel.md"
            sentinel.write_text("sentinel")
            (export_dir / "notes.md").write_text("notes")
            (export_dir / "syscall-9-custom.md").write_text("custom")
            (export_dir / "syscall-0-stale.md").write_text("stale")
            (export_dir / "syscall-0-stale-link.md").symlink_to(sentinel)
            (export_dir / "syscall-0-fake-0.md").symlink_to(sentinel)
            (export_dir / "manifest.json").write_text(
                json.dumps(
                    {
                        "section": 2,
                        "count": 4,
                        "pages": [
                            {"markdown": "syscall-0-stale.md"},
                            {"markdown": "syscall-0-stale-link.md"},
                            {"markdown": "notes.md"},
                            {"markdown": "../outside/escape.md"},
                        ],
                    }
                )
            )
            canonical, pages, sources = self._export_pages()

            generate.export_section2(
                export_dir,
                canonical,
                pages,
                sources,
                REPO_ROOT / "kernel/src",
                ABI_HEADER,
                trusted_root=root,
            )

            current = json.loads((export_dir / "manifest.json").read_text())
            generated = {page["markdown"] for page in current["pages"]}
            self.assertEqual(current["count"], 70)
            self.assertEqual(len(generated), 70)
            self.assertTrue(all((export_dir / name).is_file() for name in generated))
            self.assertFalse((export_dir / "syscall-0-stale.md").exists())
            self.assertFalse((export_dir / "syscall-0-stale-link.md").exists())
            self.assertFalse((export_dir / "syscall-0-fake-0.md").is_symlink())
            self.assertTrue((export_dir / "syscall-0-fake-0.md").is_file())
            self.assertEqual(sentinel.read_text(), "sentinel")
            self.assertTrue((export_dir / "notes.md").exists())
            self.assertTrue((export_dir / "syscall-9-custom.md").exists())
            self.assertFalse((outside / "escape.md").exists())

    def test_export_tolerates_missing_or_malformed_manifest(self):
        cases = {
            "missing": None,
            "null-page": {"pages": [None]},
            "non-list-pages": {"pages": {}},
            "string-pages": {"pages": "bad"},
        }
        for label, manifest in cases.items():
            with self.subTest(label=label):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    export_dir = root / "section2"
                    export_dir.mkdir()
                    (export_dir / "notes.md").write_text("notes")
                    (export_dir / "syscall-9-custom.md").write_text("custom")
                    if manifest is not None:
                        (export_dir / "manifest.json").write_text(json.dumps(manifest))
                    canonical, pages, sources = self._export_pages()

                    generate.export_section2(
                        export_dir,
                        canonical,
                        pages,
                        sources,
                        REPO_ROOT / "kernel/src",
                        ABI_HEADER,
                        trusted_root=root,
                    )

                    current = json.loads((export_dir / "manifest.json").read_text())
                    generated = {page["markdown"] for page in current["pages"]}
                    self.assertEqual(current["count"], 70)
                    self.assertEqual(len(generated), 70)
                    self.assertTrue(all((export_dir / name).is_file() for name in generated))
                    self.assertTrue((export_dir / "notes.md").exists())
                    self.assertTrue((export_dir / "syscall-9-custom.md").exists())

    def test_export_publication_failure_matrix_preserves_active_generation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            export_dir = root / "section2"
            canonical, pages, sources = self._export_pages(count=70, version="old")
            generate.export_section2(
                export_dir,
                canonical,
                pages,
                sources,
                REPO_ROOT / "kernel/src",
                ABI_HEADER,
                trusted_root=root,
            )
            snapshot = {
                path.name: path.read_bytes()
                for path in export_dir.iterdir()
                if path.is_file()
            }
            generated = [f"syscall-{index}-fake-{index}.md" for index in range(70)]
            _, updated, _ = self._export_pages(count=70, version="new")
            original_replace = generate.os.replace

            for target in (generated[0], generated[1], generated[35], generated[-1], "manifest.json"):
                with self.subTest(target=target):
                    def fail_replace(src, dst, *, src_dir_fd=None, dst_dir_fd=None):
                        if dst == target:
                            raise OSError("injected replacement failure")
                        return original_replace(
                            src, dst, src_dir_fd=src_dir_fd, dst_dir_fd=dst_dir_fd
                        )

                    with patch.object(generate.os, "replace", side_effect=fail_replace):
                        with self.assertRaises(OSError):
                            generate.export_section2(
                                export_dir,
                                canonical,
                                updated,
                                sources,
                                REPO_ROOT / "kernel/src",
                                ABI_HEADER,
                                trusted_root=root,
                            )
                    self.assertEqual(
                        {
                            path.name: path.read_bytes()
                            for path in export_dir.iterdir()
                            if path.is_file()
                        },
                        snapshot,
                    )
                    self.assertEqual(
                        [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                        [],
                    )

            with patch.object(
                generate,
                "exchange_directories",
                side_effect=OSError("injected activation failure"),
            ):
                with self.assertRaises(OSError):
                    generate.export_section2(
                        export_dir,
                        canonical,
                        updated,
                        sources,
                        REPO_ROOT / "kernel/src",
                        ABI_HEADER,
                        trusted_root=root,
                    )
            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in export_dir.iterdir()
                    if path.is_file()
                },
                snapshot,
            )

            generate.export_section2(
                export_dir,
                canonical,
                updated,
                sources,
                REPO_ROOT / "kernel/src",
                ABI_HEADER,
                trusted_root=root,
            )
            current = json.loads((export_dir / "manifest.json").read_text())
            self.assertEqual(current["count"], 70)
            self.assertEqual(len(list(export_dir.glob("syscall-*.md"))), 70)

    def test_man_publication_failure_matrix_preserves_active_generation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            old_files = {
                "nodivia.css": "old css",
                "index.html": "old index",
                "io_setup.2.html": "old page",
                "old.2.html": "old stale",
            }
            new_files = {
                "nodivia.css": "new css",
                "index.html": "new index",
                "io_setup.2.html": "new page",
            }

            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                for name, content in old_files.items():
                    transaction.output.write_atomic(name, content)
                generate.write_man_manifest(transaction.output, set(old_files))
                transaction.commit()

            (output / "custom.2.html").write_text("custom")
            (output / "architecture-custom.html").write_text("custom")
            (output / "pl").mkdir()
            (output / "pl" / "my-notes.7.html").write_text("notes")
            snapshot = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }

            original_replace = generate.os.replace

            def publish(target: str | None = None, activation: bool = False):
                def fail_replace(src, dst, *, src_dir_fd=None, dst_dir_fd=None):
                    if dst == target:
                        raise OSError("injected replacement failure")
                    return original_replace(
                        src, dst, src_dir_fd=src_dir_fd, dst_dir_fd=dst_dir_fd
                    )

                if activation:
                    context = patch.object(
                        generate,
                        "exchange_directories",
                        side_effect=OSError("injected activation failure"),
                    )
                else:
                    context = patch.object(
                        generate.os, "replace", side_effect=fail_replace
                    )
                with context:
                    with generate.TransactionalDirectory.begin(
                        output,
                        root,
                        generate.MAN7_MANIFEST,
                        generate.read_owned_files,
                    ) as transaction:
                        for name, content in new_files.items():
                            transaction.output.write_atomic(name, content)
                        generate.write_man_manifest(transaction.output, set(new_files))
                        transaction.commit()

            for target in ("nodivia.css", "index.html", "io_setup.2.html", generate.MAN7_MANIFEST):
                with self.subTest(target=target):
                    with self.assertRaises(OSError):
                        publish(target=target)
                    self.assertEqual(
                        {
                            path.name: path.read_bytes()
                            for path in output.iterdir()
                            if path.is_file()
                        },
                        snapshot,
                    )
                    self.assertEqual(
                        [path for path in root.iterdir() if path.name.startswith(".astia-stage-")],
                        [],
                    )

            with self.assertRaises(OSError):
                publish(activation=True)
            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                snapshot,
            )

            with generate.TransactionalDirectory.begin(
                output, root, generate.MAN7_MANIFEST, generate.read_owned_files
            ) as transaction:
                for name, content in new_files.items():
                    transaction.output.write_atomic(name, content)
                generate.write_man_manifest(transaction.output, set(new_files))
                transaction.commit()

            self.assertEqual((output / "nodivia.css").read_text(), "new css")
            self.assertEqual((output / "index.html").read_text(), "new index")
            self.assertEqual((output / "io_setup.2.html").read_text(), "new page")
            self.assertFalse((output / "old.2.html").exists())
            self.assertEqual((output / "custom.2.html").read_text(), "custom")
            self.assertEqual((output / "architecture-custom.html").read_text(), "custom")
            self.assertEqual((output / "pl" / "my-notes.7.html").read_text(), "notes")


if __name__ == "__main__":
    unittest.main()
