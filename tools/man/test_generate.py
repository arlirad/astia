import errno
import io
import json
import os
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import unittest
import warnings
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
from contextlib import redirect_stderr
from unittest.mock import patch

TOOLS_MAN = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_MAN.parents[1]
ABI_HEADER = REPO_ROOT / "kernel/include/astia/api/syscalls.h"
LIBC_ROOT = REPO_ROOT / "libc"
sys.path.insert(0, str(TOOLS_MAN))

import docs_manifest
import generate


class GenerateTest(unittest.TestCase):
    EXPECTED_SOURCES = {
        'io_setup': 'kernel/src/fs/syscalls/io_setup.cpp',
        'io_destroy': 'kernel/src/fs/syscalls/io_destroy.cpp',
        'io_submit': 'kernel/src/fs/syscalls/io_submit.cpp',
        'io_register': 'kernel/src/fs/syscalls/io_register.cpp',
        'mmap': 'kernel/src/proc/syscalls/memory/mmap.cpp',
        'munmap': 'kernel/src/proc/syscalls/memory/munmap.cpp',
        'mprotect': 'kernel/include/astia/api/syscalls.h',
        'clock_gettime': 'kernel/include/astia/api/syscalls.h',
        'exit': 'kernel/src/proc/syscalls/process/exit.cpp',
        'yield': 'kernel/src/proc/syscalls/thread/yield.cpp',
        'getpid': 'kernel/src/proc/syscalls/getpid.cpp',
        'gettid': 'kernel/src/proc/syscalls/gettid.cpp',
        'futex': 'kernel/src/proc/syscalls/ipc/futex.cpp',
        'waitpid': 'kernel/include/astia/api/syscalls.h',
        'spawnve': 'kernel/src/proc/syscalls/process/spawnve.cpp',
        'fork': 'kernel/src/proc/syscalls/process/fork.cpp',
        'thread_create': 'kernel/src/proc/syscalls/thread/thread_create.cpp',
        'thread_exit': 'kernel/src/proc/syscalls/thread/thread_exit.cpp',
        'thread_join': 'kernel/src/proc/syscalls/thread/thread_join.cpp',
        'thread_detach': 'kernel/src/proc/syscalls/thread/thread_detach.cpp',
        'kill': 'kernel/src/proc/syscalls/kill.cpp',
        'sigaction': 'kernel/src/proc/syscalls/signal/sigaction.cpp',
        'sigprocmask': 'kernel/src/proc/syscalls/signal/sigprocmask.cpp',
        'thread_create2': 'kernel/src/proc/syscalls/thread/thread_create2.cpp',
        'execve': 'libc/src/unistd.c',
        'sigsuspend': 'libc/src/signal.c',
        'getpgrp': 'kernel/src/proc/syscalls/getpgrp.cpp',
        'setpgid': 'kernel/src/proc/syscalls/setpgid.cpp',
        'setsid': 'kernel/src/proc/syscalls/setsid.cpp',
        'sigreturn': 'kernel/src/proc/syscalls/signal/sigreturn.cpp',
        'chdir': 'kernel/src/proc/syscalls/process/chdir.cpp',
        'getcwd': 'kernel/src/proc/syscalls/process/getcwd.cpp',
        'getuid': 'kernel/include/astia/api/syscalls.h',
        'geteuid': 'kernel/include/astia/api/syscalls.h',
        'getgid': 'kernel/include/astia/api/syscalls.h',
        'getegid': 'kernel/include/astia/api/syscalls.h',
        'setuid': 'kernel/src/proc/process.hpp',
        'seteuid': 'kernel/src/proc/process.hpp',
        'setgid': 'kernel/src/proc/process.hpp',
        'setegid': 'kernel/src/proc/process.hpp',
        'setreuid': 'kernel/src/proc/process.hpp',
        'setregid': 'kernel/src/proc/process.hpp',
        'getgroups': 'kernel/src/proc/process.hpp',
        'setgroups': 'kernel/src/proc/process.hpp',
        'process_snapshot': 'kernel/src/proc/proc.cpp',
        'getsid': 'kernel/src/proc/proc.cpp',
        'unmount': 'kernel/src/fs/syscalls/unmount.cpp',
        'mount': 'kernel/src/fs/syscalls/mount.cpp',
        'getrlimit': 'kernel/include/astia/api/syscalls.h',
        'setrlimit': 'kernel/include/astia/api/syscalls.h',
        'sigwait': 'kernel/include/astia/api/syscalls.h',
        'sigaltstack': 'kernel/include/astia/api/syscalls.h',
        'getpgid': 'kernel/src/proc/syscalls/getpgid.cpp',
        'getppid': 'kernel/src/proc/syscalls/getppid.cpp',
        'thread_cancel': 'kernel/include/astia/api/syscalls.h',
        'alarm': 'kernel/src/proc/syscalls/alarm.cpp',
        'thread_create_owned': 'kernel/include/astia/api/syscalls.h',
        'thread_cancel_pending': 'kernel/include/astia/api/syscalls.h',
        'thread_exists': 'kernel/include/astia/api/syscalls.h',
        'thread_kill': 'kernel/include/astia/api/syscalls.h',
        'sigpending': 'kernel/include/astia/api/syscalls.h',
        'sigtimedwait': 'kernel/include/astia/api/syscalls.h',
        'clock_getres': 'kernel/include/astia/api/syscalls.h',
        'thread_getattr': 'kernel/include/astia/api/syscalls.h',
        'msync': 'kernel/include/astia/api/syscalls.h',
        'lseek': 'kernel/include/astia/api/syscalls.h',
        'umask': 'kernel/include/astia/api/syscalls.h',
        'shm_open': 'kernel/include/astia/api/syscalls.h',
        'shm_unlink': 'kernel/include/astia/api/syscalls.h',
        'fchdir': 'kernel/include/astia/api/syscalls.h',
        'uname': 'kernel/src/proc/syscalls/process/uname.cpp',
    }
    EXPECTED_TAXONOMY = {
        'io_setup': ('fs', 'fs'),
        'io_destroy': ('fs', 'fs'),
        'io_submit': ('fs', 'fs'),
        'io_register': ('fs', 'fs'),
        'mmap': ('proc', 'memory'),
        'munmap': ('proc', 'memory'),
        'mprotect': ('abi', 'abi'),
        'clock_gettime': ('abi', 'abi'),
        'exit': ('proc', 'process'),
        'yield': ('proc', 'thread'),
        'getpid': ('proc', 'core'),
        'gettid': ('proc', 'core'),
        'futex': ('proc', 'ipc'),
        'waitpid': ('proc', 'process'),
        'spawnve': ('proc', 'process'),
        'fork': ('proc', 'process'),
        'thread_create': ('proc', 'thread'),
        'thread_exit': ('proc', 'thread'),
        'thread_join': ('proc', 'thread'),
        'thread_detach': ('proc', 'thread'),
        'kill': ('proc', 'core'),
        'sigaction': ('proc', 'signal'),
        'sigprocmask': ('proc', 'signal'),
        'thread_create2': ('proc', 'thread'),
        'execve': ('proc', 'process'),
        'sigsuspend': ('proc', 'signal'),
        'getpgrp': ('proc', 'core'),
        'setpgid': ('proc', 'core'),
        'setsid': ('proc', 'core'),
        'sigreturn': ('proc', 'signal'),
        'chdir': ('proc', 'process'),
        'getcwd': ('proc', 'process'),
        'getuid': ('abi', 'abi'),
        'geteuid': ('abi', 'abi'),
        'getgid': ('abi', 'abi'),
        'getegid': ('abi', 'abi'),
        'setuid': ('proc', 'process'),
        'seteuid': ('proc', 'process'),
        'setgid': ('proc', 'process'),
        'setegid': ('proc', 'process'),
        'setreuid': ('proc', 'process'),
        'setregid': ('proc', 'process'),
        'getgroups': ('proc', 'process'),
        'setgroups': ('proc', 'process'),
        'process_snapshot': ('proc', 'process'),
        'getsid': ('proc', 'process'),
        'unmount': ('fs', 'fs'),
        'mount': ('fs', 'fs'),
        'getrlimit': ('abi', 'abi'),
        'setrlimit': ('abi', 'abi'),
        'sigwait': ('abi', 'abi'),
        'sigaltstack': ('abi', 'abi'),
        'getpgid': ('proc', 'core'),
        'getppid': ('proc', 'core'),
        'thread_cancel': ('abi', 'abi'),
        'alarm': ('proc', 'core'),
        'thread_create_owned': ('abi', 'abi'),
        'thread_cancel_pending': ('abi', 'abi'),
        'thread_exists': ('abi', 'abi'),
        'thread_kill': ('abi', 'abi'),
        'sigpending': ('abi', 'abi'),
        'sigtimedwait': ('abi', 'abi'),
        'clock_getres': ('abi', 'abi'),
        'thread_getattr': ('abi', 'abi'),
        'msync': ('abi', 'abi'),
        'lseek': ('abi', 'abi'),
        'umask': ('abi', 'abi'),
        'shm_open': ('abi', 'abi'),
        'shm_unlink': ('abi', 'abi'),
        'fchdir': ('abi', 'abi'),
        'uname': ('proc', 'process'),
    }
    EXPECTED_SHARED_SOURCES = {
        'kernel/include/astia/api/syscalls.h': 26,
        'kernel/src/proc/process.hpp': 8,
        'kernel/src/proc/proc.cpp': 2,
    }
    EXPECTED_SECTION7 = {
        'aio': ('docs-aio-7', 'docs-7-aio', 'docs/7/aio.md', 'Asynchronous I/O'),
        'alarm': ('docs-alarm-7', 'docs-7-alarm', 'docs/7/alarm.md', 'Process alarm'),
        'exec': ('docs-exec-7', 'docs-7-exec', 'docs/7/exec.md', 'Asynchronous ELF loading and execve'),
        'rcu': ('docs-rcu-7', 'docs-7-rcu', 'docs/7/rcu.md', 'RCU and lockless constraints'),
        'sigtimedwait': ('docs-sigtimedwait-7', 'docs-7-sigtimedwait', 'docs/7/sigtimedwait.md', 'sigtimedwait(2)'),
        'spawnve': ('docs-spawnve-7', 'docs-7-spawnve', 'docs/7/spawnve.md', 'Asynchronous process spawn and spawnve'),
        'task': ('docs-task-7', 'docs-7-task', 'docs/7/task.md', 'Kernel coroutine tasks'),
    }

    def test_public_abi_prototypes_are_c_syntax(self):
        prototypes = generate.public_abi_prototypes(ABI_HEADER)
        self.assertEqual(prototypes["sigwait"], "long astia_sigwait(const uint32_t* set, int32_t* signal);")
        self.assertEqual(prototypes["alarm"], "long astia_alarm(uint32_t seconds);")
        self.assertEqual(prototypes["io_submit"], "long astia_io_submit(astia_rings* ring, uint32_t to_submit);")
        for prototype in prototypes.values():
            self.assertTrue(prototype.endswith(";"))

    def test_authored_section2_metadata_is_exact(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        canonical = generate.syscall_abi(ABI_HEADER)
        handlers = {
            path.stem: path
            for path in (REPO_ROOT / "kernel/src").glob("*/syscalls/**/*.cpp")
        }
        self.assertEqual([(page["id"], page["name"]) for page in docs.section2], canonical)
        self.assertEqual(docs.section2[0]["id"], 0)
        self.assertEqual(docs.section2[-1]["id"], 70)
        slugs = [page["slug"] for page in docs.section2]
        self.assertEqual(len(slugs), len(set(slugs)))
        for page in docs.section2:
            name = page["name"]
            subsystem, category = self.EXPECTED_TAXONOMY[name]
            suffix = f"{category}/{name}.md" if subsystem == "proc" else f"{name}.md"
            expected_path = f"docs/2/{subsystem}/{suffix}"
            self.assertEqual(page["slug"], f"syscall-{page['id']}-{name.replace('_', '-')}")
            self.assertEqual(page["title"], f"{name}(2)")
            self.assertEqual(page["source"], self.EXPECTED_SOURCES[name])
            self.assertEqual(page["subsystem"], subsystem)
            self.assertEqual(page["category"], category)
            self.assertEqual(page["sourcePath"], expected_path)
            self.assertEqual(page["synopsisKind"], "raw" if name == "sigreturn" else "public")
            self.assertEqual((REPO_ROOT / page["source"]).is_file(), True, page["source"])
            self.assertEqual((REPO_ROOT / page["sourcePath"]).is_file(), True, page["sourcePath"])
            self.assertIn(name, handlers)
            if subsystem == "proc":
                relative = handlers[name].relative_to(REPO_ROOT / "kernel/src/proc/syscalls")
                expected_category = "core" if len(relative.parts) == 1 else relative.parts[0]
                self.assertEqual(category, expected_category)
        self.assertEqual(
            sorted(page["name"] for page in docs.section2 if page["synopsisKind"] == "raw"),
            ["sigreturn"],
        )

    def test_authored_section2_synopses_match_abi_and_wrappers(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        prototypes = generate.public_abi_prototypes(ABI_HEADER)
        special = {
            "execve": "int execve(const char* path, char* const argv[], char* const envp[]);",
            "sigsuspend": "int sigsuspend(const sigset_t* mask);",
            "waitpid": "long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);",
        }
        for page in docs.section2:
            name = page["name"]
            if name == "sigreturn":
                self.assertEqual(page["synopsis"], docs_manifest.RAW_SYNOPSIS)
            else:
                self.assertEqual(page["synopsis"], special.get(name, prototypes.get(name)), name)

    def test_authored_markdown_presence_and_structure(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        for page in docs.section2:
            text = (REPO_ROOT / page["sourcePath"]).read_text()
            self.assertTrue(text.startswith(f"# {page['name']}(2)\n"))
            self.assertIn("## SYNOPSIS", text)
            self.assertIn("## DESCRIPTION", text)
            self.assertIn(page["synopsis"], text)
        self.assertEqual(
            sorted(
                path.relative_to(REPO_ROOT).as_posix()
                for path in (REPO_ROOT / "docs/2").rglob("*.md")
            ),
            sorted(page["sourcePath"] for page in docs.section2),
        )
        self.assertEqual(list((REPO_ROOT / "docs/2").glob("*.md")), [])

    def test_shared_source_paths_are_exact_and_otherwise_unique(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        counts = Counter(page["source"] for page in docs.section2)
        shared = {source: count for source, count in counts.items() if count > 1}
        self.assertEqual(shared, self.EXPECTED_SHARED_SOURCES)
        self.assertEqual(set(counts), set(self.EXPECTED_SOURCES.values()))
        self.assertEqual(len(set(self.EXPECTED_SOURCES.values())), 38)
        self.assertEqual(len(docs.section2), 71)

    def test_section7_authored_pages_and_aliases_are_exact(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        self.assertEqual({page["name"] for page in docs.section7}, set(self.EXPECTED_SECTION7))
        self.assertEqual(sorted(path.name for path in (REPO_ROOT / "docs/7").glob("*.md")), sorted(f"{name}.md" for name in self.EXPECTED_SECTION7))
        seen = set()
        for page in docs.section7:
            slug, alias, source, title = self.EXPECTED_SECTION7[page["name"]]
            self.assertEqual(page["slug"], slug)
            self.assertEqual(page["aliases"], [alias])
            self.assertEqual(page["source"], source)
            self.assertEqual(page["title"], title)
            self.assertNotIn(slug, seen)
            seen.add(slug)
            self.assertNotIn(alias, seen)
            seen.add(alias)
            self.assertTrue((REPO_ROOT / source).is_file())

    def test_migration_contains_only_section2_section7_and_metadata(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        authored = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "docs/2").rglob("*")
            if path.is_file()
        }
        overviews = {path.name for path in (REPO_ROOT / "docs/7").glob("*")}
        self.assertEqual(authored, {page["sourcePath"] for page in docs.section2})
        self.assertEqual(overviews, {f"{page['name']}.md" for page in docs.section7})
        names = {path.name for path in (REPO_ROOT / "docs/2").rglob("*.md")}
        for name in ("aarch64-virt-boot", "README", "ctest", "module-abi", "architecture-boundaries"):
            self.assertNotIn(f"{name}.md", names)
        self.assertEqual(len(generate.libc_pages()), 51)
        self.assertTrue(all(section == 3 for _, _, _, section in generate.libc_pages()))
        libc_names = {name for name, _, _, _ in generate.libc_pages()}
        self.assertEqual(len(libc_names), 51)
        self.assertEqual(
            {page["name"] for page in docs.section2},
            {name for _, name in generate.syscall_abi(ABI_HEADER)},
        )

    def test_docs2_taxonomy_directories_and_counts_are_exact(self):
        docs = docs_manifest.load_authored_docs(REPO_ROOT)
        counts = Counter((page["subsystem"], page["category"]) for page in docs.section2)
        self.assertEqual(
            counts,
            Counter({
                ("abi", "abi"): 25,
                ("fs", "fs"): 6,
                ("proc", "core"): 9,
                ("proc", "ipc"): 1,
                ("proc", "memory"): 2,
                ("proc", "process"): 18,
                ("proc", "signal"): 4,
                ("proc", "thread"): 6,
            }),
        )
        directories = {
            path.relative_to(REPO_ROOT / "docs/2").as_posix()
            for path in (REPO_ROOT / "docs/2").rglob("*")
            if path.is_dir()
        }
        self.assertEqual(
            directories,
            {
                "abi",
                "fs",
                "proc",
                "proc/core",
                "proc/ipc",
                "proc/memory",
                "proc/process",
                "proc/signal",
                "proc/thread",
            },
        )
        self.assertEqual(len({page["sourcePath"] for page in docs.section2}), 71)

    def test_manifest_has_no_markdown_bodies(self):
        manifest = json.loads((REPO_ROOT / "docs/manifest.json").read_text())
        serialized = json.dumps(manifest)
        self.assertNotIn('"content"', serialized)
        for page in manifest["section2"]["pages"]:
            self.assertNotIn("markdown", page)
        for page in manifest["section7"]["pages"]:
            self.assertNotIn("markdown", page)
        generated = (REPO_ROOT / "frontend/src/app/docs/docs.manifest.ts").read_text()
        self.assertNotRegex(generated, r"\bcontent\s*:")
        self.assertNotIn("# io_setup(2)", generated)
        for page in docs_manifest.load_authored_docs(REPO_ROOT).section2:
            body = (REPO_ROOT / page["sourcePath"]).read_text()
            self.assertNotIn(body.strip(), generated)

    def test_handler_documentation_blocks_are_removed(self):
        block = re.compile(r"(?m)^[ \t]*///[ \t]*(\w+)[ \t]*-[ \t]*")
        for path in (REPO_ROOT / "kernel/src").glob("*/syscalls/**/*.cpp"):
            text = path.read_text()
            self.assertNotRegex(text, r"(?m)^\s*///", path)
            self.assertIsNone(block.search(text), path)
        for path in (REPO_ROOT / "kernel/src").glob("*/syscalls/**/*.hpp"):
            self.assertIsNone(block.search(path.read_text()), path)

    def test_execve_waitpid_and_sigsuspend_semantics(self):
        execve = (REPO_ROOT / "docs/2/proc/process/execve.md").read_text()
        self.assertIn("-1 and sets errno", execve)
        self.assertIn("FD_CLOEXEC", execve)
        self.assertIn("null path returns EINVAL", execve)
        self.assertIn("ENAMETOOLONG", execve)
        self.assertIn("libc/src/unistd.c", execve)
        self.assertIn("exec_loader.cpp", execve)
        waitpid = (REPO_ROOT / "docs/2/proc/process/waitpid.md").read_text()
        self.assertIn("astia_waitpid_options", waitpid)
        self.assertIn("WNOHANG", waitpid)
        self.assertIn("WUNTRACED", waitpid)
        self.assertIn("WCONTINUED", waitpid)
        self.assertIn("-EAGAIN", waitpid)
        self.assertIn("-EINTR", waitpid)
        self.assertIn("without a wake slot", waitpid)
        self.assertIn("negative errno", waitpid)
        sigsuspend = (REPO_ROOT / "docs/2/proc/signal/sigsuspend.md").read_text()
        self.assertIn("Never returns 0", sigsuspend)
        self.assertIn("errno to EINTR", sigsuspend)
        self.assertIn("EAGAIN", sigsuspend)
        self.assertIn("libc/src/signal.c", sigsuspend)

    def test_generator_exports_authored_markdown_verbatim(self):
        with tempfile.TemporaryDirectory(dir=REPO_ROOT) as temporary:
            root = Path(temporary)
            subprocess.run(
                [
                    sys.executable,
                    str(TOOLS_MAN / "generate.py"),
                    str(REPO_ROOT / "kernel/src"),
                    str(root / "site"),
                    "--docs",
                    str(REPO_ROOT / "docs"),
                    "--export-section2",
                    str(root / "section2"),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
            manifest = json.loads((root / "section2/manifest.json").read_text())
            docs = docs_manifest.load_authored_docs(REPO_ROOT)
            self.assertEqual(manifest["count"], 71)
            for page in docs.section2:
                exported = (root / "section2" / f"{page['slug']}.md").read_text()
                authored = (REPO_ROOT / page["sourcePath"]).read_text()
                self.assertEqual(exported, authored, page["name"])
            for page in docs.section7:
                self.assertTrue((root / "site" / f"{page['name']}.7.html").is_file())

    def test_static_man_index_nests_proc_taxonomy_with_exact_counts(self):
        class IndexParser(HTMLParser):
            def __init__(self) -> None:
                super().__init__()
                self.stack: list[str | None] = []
                self.pending: list[str] | None = None
                self.current: str | None = None
                self.parents: dict[str, tuple[str, ...]] = {}
                self.links: dict[str, int] = {}

            def handle_starttag(self, tag, attrs):
                if tag == "section":
                    self.stack.append(None)
                elif tag == "h3":
                    self.pending = []
                elif tag == "li" and self.current is not None:
                    self.links[self.current] += 1

            def handle_data(self, data):
                if self.pending is not None:
                    self.pending.append(data)

            def handle_endtag(self, tag):
                if tag == "h3" and self.pending is not None:
                    name = "".join(self.pending).strip()
                    self.pending = None
                    self.parents[name] = tuple(value for value in self.stack if value)
                    if self.stack:
                        self.stack[-1] = name
                    self.current = name
                    self.links.setdefault(name, 0)
                elif tag == "section" and self.stack:
                    self.stack.pop()

        with tempfile.TemporaryDirectory(dir=REPO_ROOT) as temporary:
            root = Path(temporary)
            subprocess.run(
                [
                    sys.executable,
                    str(TOOLS_MAN / "generate.py"),
                    str(REPO_ROOT / "kernel/src"),
                    str(root / "site"),
                    "--docs",
                    str(REPO_ROOT / "docs"),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
            parser = IndexParser()
            parser.feed((root / "site/index.html").read_text())
            expected = {
                "core": 9,
                "ipc": 1,
                "memory": 2,
                "process": 18,
                "signal": 4,
                "thread": 6,
            }
            for category, count in expected.items():
                self.assertEqual(parser.parents[category], ("proc",), category)
                self.assertEqual(parser.links[category], count, category)
            self.assertEqual(sum(expected.values()), 40)
            self.assertEqual(parser.parents["proc"], ())
            self.assertEqual(parser.parents["fs"], ())
            self.assertEqual(parser.parents["abi"], ())
            self.assertEqual(parser.links["fs"], 6)
            self.assertEqual(parser.links["abi"], 25)

    def test_export_fallback_source_paths_follow_the_taxonomy_layout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            export_dir = root / "section2"
            canonical, pages, _ = self._export_pages(count=3)
            sources = {
                (name, 2): REPO_ROOT / "kernel/src/proc/syscalls/getpid.cpp"
                for _, name in canonical
            }
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
            self.assertEqual(
                [page["source_path"] for page in current["pages"]],
                [f"docs/2/proc/core/fake_{index}.md" for index in range(3)],
            )
            self.assertEqual(
                {(page["subsystem"], page["category"]) for page in current["pages"]},
                {("proc", "core")},
            )

    def _export_pages(self, count: int = 70, version: str = "old"):
        canonical = [(index, f"fake_{index}") for index in range(count)]
        pages = {
            (name, 2): (
                f"Synopsis:\n    long {name}_{version}(void);\n\n"
                "Description:\n    Synthetic export page.",
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
                    "clear_directory",
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
                path
                for path in root.iterdir()
                if path.name.startswith(".astia-clean-")
                or path.name.startswith(".astia-stage-")
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
    def test_transaction_aborts_when_active_hash_read_fails(self):
        for error_number in (errno.EACCES, errno.EIO):
            with self.subTest(error_number=error_number):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output = self._old_man_generation(root)
                    before = {
                        path.name: path.read_bytes()
                        for path in output.iterdir()
                        if path.is_file()
                    }
                    real_open = generate.os.open

                    def fail_hash_open(path, flags, mode=0o777, *, dir_fd=None):
                        if path == "index.html" and (flags & os.O_ACCMODE) == os.O_RDONLY:
                            raise OSError(
                                error_number, os.strerror(error_number)
                            )
                        return real_open(path, flags, mode, dir_fd=dir_fd)

                    with patch.object(
                        generate.os, "open", side_effect=fail_hash_open
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
                        [
                            path
                            for path in root.iterdir()
                            if path.name.startswith(".astia-stage-")
                            or path.name.startswith(".astia-retired-")
                        ],
                        [],
                    )

    def test_content_digest_alone_aborts_in_place_edit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            page = output / "index.html"
            original_stat = page.stat()
            original_manifest = (output / generate.MAN7_MANIFEST).read_bytes()
            real_metadata = generate._entry_metadata

            def coarsened_metadata(descriptor, name):
                metadata = real_metadata(descriptor, name)
                return metadata[:7] + (0,) + metadata[8:]

            def edit_in_place():
                current = page.read_bytes()
                replacement = b"N" + current[1:]
                self.assertEqual(len(current), len(replacement))
                with page.open("r+b") as handle:
                    handle.write(replacement)
                os.utime(
                    page,
                    ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns),
                )

            with patch.object(
                generate, "_entry_metadata", side_effect=coarsened_metadata
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
                        transaction.commit(before_activation=edit_in_place)

            self.assertTrue(page.read_bytes().startswith(b"N"))
            self.assertEqual(
                (output / generate.MAN7_MANIFEST).read_bytes(), original_manifest
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

    def test_copy_aborts_if_regular_file_becomes_fifo_at_open(self):
        def timeout(_signum, _frame):
            raise TimeoutError("copy open blocked")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            payload = output / "payload.bin"
            payload.write_bytes(b"payload")
            original_index = (output / "index.html").read_bytes()
            original_manifest = (output / generate.MAN7_MANIFEST).read_bytes()
            real_open = generate.os.open
            calls = 0

            def replace_at_open(path, flags, mode=0o777, *, dir_fd=None):
                nonlocal calls
                if path == "payload.bin":
                    calls += 1
                    if calls == 2:
                        payload.unlink()
                        os.mkfifo(payload)
                return real_open(path, flags, mode, dir_fd=dir_fd)

            previous_handler = signal.signal(signal.SIGALRM, timeout)
            signal.setitimer(signal.ITIMER_REAL, 2)
            try:
                with patch.object(
                    generate.os, "open", side_effect=replace_at_open
                ):
                    with self.assertRaises(ValueError):
                        generate.TransactionalDirectory.begin(
                            output,
                            root,
                            generate.MAN7_MANIFEST,
                            generate.read_owned_files,
                        )
            finally:
                signal.setitimer(signal.ITIMER_REAL, 0)
                signal.signal(signal.SIGALRM, previous_handler)

            self.assertTrue(stat.S_ISFIFO(os.lstat(payload).st_mode))
            self.assertEqual((output / "index.html").read_bytes(), original_index)
            self.assertEqual(
                (output / generate.MAN7_MANIFEST).read_bytes(), original_manifest
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

    def test_transaction_aborts_if_owned_page_is_edited_in_place(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            page = output / "index.html"
            original_stat = page.stat()
            original_manifest = (output / generate.MAN7_MANIFEST).read_bytes()

            def edit_in_place():
                current = page.read_bytes()
                replacement = b"N" + current[1:]
                self.assertEqual(len(current), len(replacement))
                with page.open("r+b") as handle:
                    handle.write(replacement)
                os.utime(
                    page,
                    ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns),
                )

            with self.assertRaises(RuntimeError):
                with generate.TransactionalDirectory.begin(
                    output, root, generate.MAN7_MANIFEST, generate.read_owned_files
                ) as transaction:
                    transaction.output.write_atomic("index.html", "new index")
                    generate.write_man_manifest(transaction.output, {"index.html"})
                    transaction.commit(before_activation=edit_in_place)

            self.assertTrue(page.read_bytes().startswith(b"N"))
            self.assertEqual(
                (output / generate.MAN7_MANIFEST).read_bytes(), original_manifest
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

    def test_post_exchange_snapshot_error_does_not_fail_activation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            real_snapshot = generate.directory_snapshot
            calls = 0
            stderr = io.StringIO()

            def fail_retirement_snapshot(directory, prefix=""):
                nonlocal calls
                calls += 1
                if calls == 4:
                    raise RuntimeError("snapshot-read-EIO")
                return real_snapshot(directory, prefix)

            with warnings.catch_warnings():
                warnings.simplefilter("error")
                with redirect_stderr(stderr):
                    with patch.object(
                        generate,
                        "directory_snapshot",
                        side_effect=fail_retirement_snapshot,
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
            retired = [
                path
                for path in root.iterdir()
                if path.name.startswith(".astia-retired-")
            ]
            self.assertEqual(len(retired), 1)
            self.assertEqual((retired[0] / "index.html").read_text(), "old index")
            diagnostic = stderr.getvalue()
            self.assertIn("deferred cleanup", diagnostic)
            self.assertIn(str(retired[0].resolve()), diagnostic)
            self.assertIn("snapshot-read-EIO", diagnostic)
            shutil.rmtree(retired[0])

    def test_stage_replacement_before_identity_check_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            fixed = b"\x02" * 8
            candidate_name = f".astia-stage-{fixed.hex()}"
            backup_name = "original-stage"
            real_urandom = generate.os.urandom
            real_stat = generate.os.stat
            replaced = False
            before = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }

            def urandom(size):
                return fixed if size == 8 else real_urandom(size)

            def replace_on_stat(path, *, dir_fd=None, follow_symlinks=True):
                nonlocal replaced
                if (
                    not replaced
                    and path == candidate_name
                    and dir_fd is not None
                ):
                    replaced = True
                    os.rename(
                        candidate_name,
                        backup_name,
                        src_dir_fd=dir_fd,
                        dst_dir_fd=dir_fd,
                    )
                    os.mkdir(candidate_name, 0o700, dir_fd=dir_fd)
                    replacement_dir = os.open(
                        candidate_name,
                        os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                        dir_fd=dir_fd,
                    )
                    try:
                        descriptor = os.open(
                            "replacement",
                            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                            0o600,
                            dir_fd=replacement_dir,
                        )
                        try:
                            os.write(descriptor, b"replacement")
                        finally:
                            os.close(descriptor)
                    finally:
                        os.close(replacement_dir)
                return real_stat(
                    path, dir_fd=dir_fd, follow_symlinks=follow_symlinks
                )

            with patch.object(generate.os, "urandom", side_effect=urandom), patch.object(
                generate.os, "stat", side_effect=replace_on_stat
            ):
                with self.assertRaises(RuntimeError):
                    generate.TransactionalDirectory.begin(
                        output,
                        root,
                        generate.MAN7_MANIFEST,
                        generate.read_owned_files,
                    )

            replacement = root / candidate_name
            backup = root / backup_name
            self.assertTrue(replacement.is_dir())
            self.assertEqual((replacement / "replacement").read_bytes(), b"replacement")
            self.assertTrue(backup.is_dir())
            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                before,
            )

    def test_cleanup_replacement_between_identity_check_and_removal_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / ".astia-stage-cleanup"
            stage.mkdir()
            (stage / "old.bin").write_bytes(b"old")
            backup = root / "original-stage"
            parent = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            stage_fd = os.open(stage, os.O_RDONLY | os.O_DIRECTORY)
            expected = os.fstat(stage_fd)
            real_rename = generate.rename_noreplace
            replaced = False

            def replace_then_rename(parent_fd, first, second):
                nonlocal replaced
                if not replaced and first == stage.name:
                    replaced = True
                    os.rename(
                        first,
                        backup.name,
                        src_dir_fd=parent_fd,
                        dst_dir_fd=parent_fd,
                    )
                    os.mkdir(first, 0o700, dir_fd=parent_fd)
                    replacement_dir = os.open(
                        first,
                        os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                        dir_fd=parent_fd,
                    )
                    try:
                        descriptor = os.open(
                            "replacement",
                            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                            0o600,
                            dir_fd=replacement_dir,
                        )
                        try:
                            os.write(descriptor, b"replacement")
                        finally:
                            os.close(descriptor)
                    finally:
                        os.close(replacement_dir)
                return real_rename(parent_fd, first, second)

            try:
                with patch.object(
                    generate, "rename_noreplace", side_effect=replace_then_rename
                ):
                    removed, retained, error = generate.quarantine_and_remove(
                        parent,
                        stage.name,
                        expected,
                        stage_fd,
                        ".astia-clean-",
                    )
            finally:
                os.close(stage_fd)
                os.close(parent)

            self.assertFalse(removed)
            self.assertIsNotNone(error)
            retained_path = root / retained
            self.assertEqual((retained_path / "replacement").read_bytes(), b"replacement")
            self.assertEqual((backup / "old.bin").read_bytes(), b"old")
            self.assertFalse((retained_path / "old.bin").exists())

    def test_symlink_collision_diagnostics_are_lexical(self):
        for kind in ("loop", "target"):
            with self.subTest(kind=kind):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output = self._old_man_generation(root)
                    fixed = b"\x03" * 8
                    candidate_name = f".astia-stage-{fixed.hex()}"
                    candidate = root / candidate_name
                    target = root / "outside-target"
                    if kind == "loop":
                        candidate.symlink_to(candidate_name)
                    else:
                        target.mkdir()
                        candidate.symlink_to(target)
                    real_urandom = generate.os.urandom
                    calls = 0

                    def urandom(size):
                        nonlocal calls
                        calls += 1
                        if calls == 1:
                            return fixed
                        raise OSError("urandom-ENOSPC")

                    stderr = io.StringIO()
                    with warnings.catch_warnings():
                        warnings.simplefilter("error")
                        with redirect_stderr(stderr):
                            with patch.object(
                                generate.os, "urandom", side_effect=urandom
                            ):
                                with self.assertRaises(OSError):
                                    generate.TransactionalDirectory.begin(
                                        output,
                                        root,
                                        generate.MAN7_MANIFEST,
                                        generate.read_owned_files,
                                    )

                    diagnostic = stderr.getvalue()
                    self.assertIn(str(candidate), diagnostic)
                    self.assertIn("urandom-ENOSPC", diagnostic)
                    if kind == "target":
                        self.assertNotIn(str(target), diagnostic)
                    self.assertEqual(os.readlink(candidate), candidate_name if kind == "loop" else str(target))

    def test_staging_collision_is_never_deleted_after_urandom_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            collision_name = ".astia-stage-0101010101010101"
            collision = root / collision_name
            collision.mkdir()
            (collision / "unrelated.bin").write_bytes(b"unrelated")
            before = {
                path.name: path.read_bytes()
                for path in output.iterdir()
                if path.is_file()
            }
            calls = 0

            def urandom(size):
                nonlocal calls
                calls += 1
                if calls == 1:
                    return b"\x01" * size
                raise OSError("urandom-ENOSPC")

            stderr = io.StringIO()
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                with redirect_stderr(stderr):
                    with patch.object(generate.os, "urandom", side_effect=urandom):
                        with self.assertRaises(OSError):
                            generate.TransactionalDirectory.begin(
                                output,
                                root,
                                generate.MAN7_MANIFEST,
                                generate.read_owned_files,
                            )

            self.assertEqual(calls, 2)
            diagnostic = stderr.getvalue()
            self.assertIn(str(collision.resolve()), diagnostic)
            self.assertIn("preserved pre-existing", diagnostic)
            self.assertIn("urandom-ENOSPC", diagnostic)
            self.assertTrue(collision.is_dir())
            self.assertEqual((collision / "unrelated.bin").read_bytes(), b"unrelated")
            self.assertEqual(
                {
                    path.name: path.read_bytes()
                    for path in output.iterdir()
                    if path.is_file()
                },
                before,
            )
            stages = {
                path.name
                for path in root.iterdir()
                if path.name.startswith(".astia-stage-")
            }
            self.assertEqual(stages, {collision_name})
            self.assertEqual(
                [
                    path
                    for path in root.iterdir()
                    if path.name.startswith(".astia-retired-")
                ],
                [],
            )

    def test_post_exchange_urandom_failure_reports_staging_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            real_snapshot = generate.directory_snapshot
            real_urandom = generate.os.urandom
            calls = 0
            failing = False
            stderr = io.StringIO()

            def fail_retirement_snapshot(directory, prefix=""):
                nonlocal calls
                calls += 1
                if calls == 4:
                    raise RuntimeError("snapshot-read-EIO")
                return real_snapshot(directory, prefix)

            def fail_urandom(size):
                if failing:
                    raise OSError("urandom-ENOSPC")
                return real_urandom(size)

            def start_failing():
                nonlocal failing
                failing = True

            with warnings.catch_warnings():
                warnings.simplefilter("error")
                with redirect_stderr(stderr):
                    with patch.object(
                        generate,
                        "directory_snapshot",
                        side_effect=fail_retirement_snapshot,
                    ), patch.object(
                        generate.os, "urandom", side_effect=fail_urandom
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
                            transaction.commit(before_activation=start_failing)

            self.assertEqual((output / "index.html").read_text(), "new index")
            staged = [
                path for path in root.iterdir() if path.name.startswith(".astia-stage-")
            ]
            retired = [
                path for path in root.iterdir() if path.name.startswith(".astia-retired-")
            ]
            self.assertEqual(len(staged), 1)
            self.assertEqual(retired, [])
            self.assertEqual((staged[0] / "index.html").read_text(), "old index")
            diagnostic = stderr.getvalue()
            self.assertIn(str(staged[0].resolve()), diagnostic)
            self.assertIn("snapshot-read-EIO", diagnostic)
            self.assertIn("urandom-ENOSPC", diagnostic)
            shutil.rmtree(staged[0])

    def test_urandom_failure_alone_reports_staging_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            real_urandom = generate.os.urandom
            failing = False
            stderr = io.StringIO()

            def fail_urandom(size):
                if failing:
                    raise OSError("urandom-ENOSPC")
                return real_urandom(size)

            def start_failing():
                nonlocal failing
                failing = True

            with warnings.catch_warnings():
                warnings.simplefilter("error")
                with redirect_stderr(stderr):
                    with patch.object(
                        generate,
                        "retirement_snapshots_match",
                        return_value=False,
                    ), patch.object(
                        generate.os, "urandom", side_effect=fail_urandom
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
                            transaction.commit(before_activation=start_failing)

            self.assertEqual((output / "index.html").read_text(), "new index")
            staged = [
                path for path in root.iterdir() if path.name.startswith(".astia-stage-")
            ]
            retired = [
                path for path in root.iterdir() if path.name.startswith(".astia-retired-")
            ]
            self.assertEqual(len(staged), 1)
            self.assertEqual(retired, [])
            self.assertEqual((staged[0] / "index.html").read_text(), "old index")
            diagnostic = stderr.getvalue()
            self.assertIn(str(staged[0].resolve()), diagnostic)
            self.assertIn("urandom-ENOSPC", diagnostic)
            shutil.rmtree(staged[0])

    def test_transaction_succeeds_when_warnings_are_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self._old_man_generation(root)
            stderr = io.StringIO()
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                with redirect_stderr(stderr):
                    with patch.object(
                        generate,
                        "clear_directory",
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
                path
                for path in root.iterdir()
                if path.name.startswith(".astia-clean-")
                or path.name.startswith(".astia-stage-")
            ]
            self.assertEqual(len(retained), 1)
            diagnostic = stderr.getvalue()
            self.assertIn("deferred cleanup", diagnostic)
            self.assertIn(retained[0].name, diagnostic)
            self.assertIn("deferred cleanup", diagnostic)
            shutil.rmtree(retained[0])

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
