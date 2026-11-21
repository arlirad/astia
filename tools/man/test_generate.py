import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_MAN = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_MAN.parents[1]
sys.path.insert(0, str(TOOLS_MAN))

import generate


class GenerateTest(unittest.TestCase):
    def test_public_abi_prototypes_are_c_syntax(self):
        prototypes = generate.public_abi_prototypes(
            REPO_ROOT / "kernel/include/astia/api/syscalls.h"
        )

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

    def test_export_removes_only_stale_generated_markdown(self):
        with tempfile.TemporaryDirectory() as temporary:
            export_dir = Path(temporary) / "section2"
            export_dir.mkdir()
            (export_dir / "stale.md").write_text("stale")
            (export_dir / "keep.txt").write_text("keep")
            (export_dir / "manifest.json").write_text("{}")

            pages = {
                ("sigwait", 2): (
                    generate.fallback_body(
                        "sigwait",
                        "long astia_sigwait(const uint32_t* set, int32_t* signal);",
                    ),
                    "proc/signal",
                )
            }
            sources = {
                ("sigwait", 2): REPO_ROOT
                / "kernel/src/proc/syscalls/signal/sigwait.cpp"
            }

            generate.export_section2(
                export_dir,
                [(50, "sigwait")],
                pages,
                sources,
                REPO_ROOT / "kernel/src",
                REPO_ROOT / "kernel/include/astia/api/syscalls.h",
            )

            markdown = sorted(path.name for path in export_dir.glob("*.md"))
            self.assertEqual(markdown, ["syscall-50-sigwait.md"])
            self.assertFalse((export_dir / "stale.md").exists())
            self.assertTrue((export_dir / "keep.txt").exists())
            manifest = json.loads((export_dir / "manifest.json").read_text())
            self.assertEqual(manifest["count"], 1)
            self.assertEqual(manifest["pages"][0]["source"], "kernel/src/proc/syscalls/signal/sigwait.cpp")


if __name__ == "__main__":
    unittest.main()
