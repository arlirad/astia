import json
import sys
import tempfile
import unittest
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

    def test_man_cleanup_uses_manifest_ownership(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            output.mkdir()
            outside.mkdir()
            (output / "custom.2.html").write_text("custom")
            (output / "architecture-custom.html").write_text("custom")
            (output / "keep.2.html").write_text("keep")
            (output / "old.2.html").write_text("old")
            (output / "architecture-old.html").write_text("old")
            (output / "pl").mkdir()
            (output / "pl" / "old.7.html").write_text("old")
            (output / "pl" / "my-notes.7.html").write_text("notes")

            with generate.OutputDirectory.open(output, root) as directory:
                generate.write_man_manifest(
                    directory,
                    {
                        "old.2.html",
                        "architecture-old.html",
                        "pl/old.7.html",
                        "keep.2.html",
                        "../outside/escape.html",
                    },
                )
                previous = generate.read_owned_files(directory)
                generate.clean_owned_output(directory, previous, {"keep.2.html"})

            self.assertFalse((output / "old.2.html").exists())
            self.assertFalse((output / "architecture-old.html").exists())
            self.assertFalse((output / "pl" / "old.7.html").exists())
            self.assertTrue((output / "keep.2.html").exists())
            self.assertTrue((output / "custom.2.html").exists())
            self.assertTrue((output / "architecture-custom.html").exists())
            self.assertTrue((output / "pl" / "my-notes.7.html").exists())
            self.assertFalse((outside / "escape.html").exists())

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
                self.assertEqual(generate.read_owned_files(directory), set())
                generate.write_man_manifest(directory, {"safe.2.html"})

            self.assertEqual(sentinel.read_text(), "sentinel")
            self.assertFalse((output / generate.MAN7_MANIFEST).is_symlink())
            self.assertEqual(
                json.loads((output / generate.MAN7_MANIFEST).read_text()),
                {"files": ["safe.2.html"]},
            )

    def test_man_cleanup_does_not_traverse_symlink_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "man7"
            outside = root / "outside"
            output.mkdir()
            outside.mkdir()
            (outside / "old.7.html").write_text("outside old")
            (outside / "my-notes.7.html").write_text("outside notes")
            (output / "pl").symlink_to(outside)

            with generate.OutputDirectory.open(output, root) as directory:
                generate.write_man_manifest(
                    directory, {"pl/old.7.html", "pl/my-notes.7.html"}
                )
                previous = generate.read_owned_files(directory)
                generate.clean_owned_output(directory, previous, set())

            self.assertTrue((output / "pl").is_symlink())
            self.assertEqual((outside / "old.7.html").read_text(), "outside old")
            self.assertEqual((outside / "my-notes.7.html").read_text(), "outside notes")

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

    def test_export_publication_failure_preserves_prior_page_and_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            export_dir = root / "section2"
            export_dir.mkdir()
            canonical, pages, sources = self._export_pages(count=2, version="old")
            generate.export_section2(
                export_dir,
                canonical,
                pages,
                sources,
                REPO_ROOT / "kernel/src",
                ABI_HEADER,
                trusted_root=root,
            )
            first = export_dir / "syscall-0-fake-0.md"
            manifest = export_dir / "manifest.json"
            before_page = first.read_text()
            before_manifest = manifest.read_text()

            _, updated, _ = self._export_pages(count=2, version="new")
            original_replace = generate.os.replace

            def fail_replace(src, dst, *, src_dir_fd=None, dst_dir_fd=None):
                if dst == "syscall-0-fake-0.md":
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

            self.assertEqual(first.read_text(), before_page)
            self.assertEqual(manifest.read_text(), before_manifest)
            self.assertEqual([path for path in export_dir.iterdir() if path.name.endswith(".tmp")], [])


if __name__ == "__main__":
    unittest.main()
