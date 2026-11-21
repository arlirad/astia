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
        self.assertEqual(generate.CATEGORY_OVERRIDES["waitpid"], "proc/process")

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

    def test_export_removes_only_previously_generated_markdown(self):
        with tempfile.TemporaryDirectory() as temporary:
            export_dir = Path(temporary) / "section2"
            export_dir.mkdir()
            (export_dir / "notes.md").write_text("unrelated notes")
            (export_dir / "keep.txt").write_text("keep")
            (export_dir / "syscall-0-old.md").write_text("stale generated")
            (export_dir / "manifest.json").write_text(
                json.dumps(
                    {
                        "section": 2,
                        "count": 1,
                        "pages": [{"markdown": "syscall-0-old.md"}],
                    }
                )
            )

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

            manifest = json.loads((export_dir / "manifest.json").read_text())
            generated = {page["markdown"] for page in manifest["pages"]}
            self.assertEqual(manifest["count"], 70)
            self.assertEqual(len(generated), 70)
            self.assertEqual(len(list(export_dir.glob("syscall-*.md"))), 70)
            self.assertTrue(all((export_dir / name).exists() for name in generated))
            self.assertTrue((export_dir / "notes.md").exists())
            self.assertTrue((export_dir / "keep.txt").exists())
            self.assertFalse((export_dir / "syscall-0-old.md").exists())


if __name__ == "__main__":
    unittest.main()
