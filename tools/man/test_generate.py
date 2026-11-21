import json
import sys
import tempfile
import unittest
from pathlib import Path

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
        self.assertIn("ESRCH", errors)
        self.assertIn("resolved against the caller's working directory by libc", notes)
        self.assertIn("execve.cpp", notes)
        self.assertIn("exec_loader.cpp", notes)
        self.assertEqual(generate.error_entries("E2BIG: Too big.")[0][0], "E2BIG")

    def test_waitpid_notes_identify_options_wrapper(self):
        body = (
            "Synopsis:\n"
            "    long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);\n\n"
            "Description:\n    Waits for a child.\n\n"
            "Return Value:\n    Negative errno.\n\n"
            "Notes:\n    Old notes."
        )
        notes = generate.PUBLIC_SECTION_OVERRIDES["waitpid"]["NOTES"]
        body = generate.replace_section(body, "NOTES", notes)
        sections = generate.body_sections(body)

        self.assertIn("astia_waitpid_options", "\n".join(sections["NOTES"]))
        self.assertIn("translates that result to -1", "\n".join(sections["NOTES"]))

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

    def _export_fixture(self, temporary: str, manifest: object | None) -> tuple[Path, dict]:
        export_dir = Path(temporary) / "section2"
        export_dir.mkdir()
        outside = Path(temporary) / "outside"
        outside.mkdir()
        (export_dir / "notes.md").write_text("unrelated notes")
        (export_dir / "keep.txt").write_text("keep")
        (export_dir / "syscall-0-stale.md").write_text("stale generated")
        (outside / "escape.md").write_text("outside")
        (export_dir / "syscall-0-fake-0.md").symlink_to(outside / "output.md")
        (export_dir / "syscall-0-stale-link.md").symlink_to(outside / "stale.md")
        if manifest is not None:
            (export_dir / "manifest.json").write_text(json.dumps(manifest))

        canonical = [(index, f"fake_{index}") for index in range(70)]
        pages = {
            (name, 2): (
                generate.fallback_body(name, f"long astia_{name}(void);"),
                "proc",
            )
            for _, name in canonical
        }
        sources = {(name, 2): ABI_HEADER for _, name in canonical}

        generate.export_section2(
            export_dir,
            canonical,
            pages,
            sources,
            REPO_ROOT / "kernel/src",
            ABI_HEADER,
        )
        return export_dir, json.loads((export_dir / "manifest.json").read_text())

    def test_export_removes_only_previously_generated_markdown(self):
        cases = {
            "missing": None,
            "valid": {
                "section": 2,
                "count": 3,
                "pages": [
                    {"markdown": "syscall-0-stale.md"},
                    {"markdown": "notes.md"},
                    {"markdown": "../outside/escape.md"},
                ],
            },
            "null-page": {"pages": [None]},
            "non-list-pages": {"pages": {}},
        }
        for label, manifest in cases.items():
            with self.subTest(label=label):
                with tempfile.TemporaryDirectory() as temporary:
                    export_dir, current = self._export_fixture(temporary, manifest)
                    generated = {page["markdown"] for page in current["pages"]}

                    self.assertEqual(current["count"], 70)
                    self.assertEqual(len(generated), 70)
                    self.assertEqual(len(list(export_dir.glob("syscall-*.md"))), 70)
                    self.assertTrue(all((export_dir / name).is_file() for name in generated))
                    self.assertTrue((export_dir / "notes.md").exists())
                    self.assertTrue((export_dir / "keep.txt").exists())
                    self.assertFalse((export_dir / "syscall-0-stale.md").exists())
                    self.assertTrue((Path(temporary) / "outside" / "escape.md").exists())

    def test_export_removes_generated_symlinks_without_following(self):
        for manifest in (None, {"pages": [None]}):
            with self.subTest(manifest=manifest):
                with tempfile.TemporaryDirectory() as temporary:
                    export_dir, _ = self._export_fixture(temporary, manifest)
                    outside = Path(temporary) / "outside"

                    self.assertFalse((outside / "output.md").exists())
                    self.assertFalse((outside / "stale.md").exists())
                    self.assertFalse((export_dir / "syscall-0-fake-0.md").is_symlink())
                    self.assertFalse((export_dir / "syscall-0-stale-link.md").is_symlink())
                    self.assertTrue((export_dir / "syscall-0-fake-0.md").is_file())
                    self.assertEqual(len(list(export_dir.glob("syscall-*.md"))), 70)


if __name__ == "__main__":
    unittest.main()
