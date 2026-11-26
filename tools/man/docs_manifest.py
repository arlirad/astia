"""Load and validate the authored documentation manifest."""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import re
from typing import Any

CANONICAL_SYSCALL_IDS = tuple(range(79))
SECTION7_NAMES = ("aio", "alarm", "exec", "rcu", "sigtimedwait", "spawnve", "task")
SECTION7_SLUGS = {
    "aio": "docs-aio-7",
    "alarm": "docs-alarm-7",
    "exec": "docs-exec-7",
    "rcu": "docs-rcu-7",
    "sigtimedwait": "docs-sigtimedwait-7",
    "spawnve": "docs-spawnve-7",
    "task": "docs-task-7",
}
SECTION7_ALIASES = {name: f"docs-7-{name}" for name in SECTION7_NAMES}
RAW_SYNOPSIS = "Raw kernel ABI. No public C wrapper declaration is provided for this syscall."
SECTION2_SUBSYSTEMS = ("abi", "dl", "fs", "mod", "proc")
PROC_CATEGORIES = ("core", "ipc", "memory", "process", "signal", "thread")
CATEGORY = re.compile(r"^[a-z0-9]+(?:[/-][a-z0-9]+)*$")
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING = re.compile(r"^\s*(#{1,6})\s+(.+?)\s*#*\s*$", re.M)
FENCE = re.compile(r"```c\n(.*?)\n```", re.S)


class DocsValidationError(ValueError):
    """Raised when authored documentation violates its contract."""


@dataclass(frozen=True)
class AuthoredDocs:
    section2: tuple[dict[str, Any], ...]
    section7: tuple[dict[str, Any], ...]

    @property
    def section2_by_name(self) -> dict[str, dict[str, Any]]:
        return {record["name"]: record for record in self.section2}

    @property
    def section7_by_name(self) -> dict[str, dict[str, Any]]:
        return {record["name"]: record for record in self.section7}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise DocsValidationError(message)


def _safe_relative(path: Any, label: str) -> Path:
    _require(isinstance(path, str) and bool(path), f"{label} must be a non-empty string")
    value = Path(path)
    _require(not value.is_absolute() and ".." not in value.parts, f"{label} is not repository-relative")
    return value


def _read(path: Path) -> str:
    _require(path.is_file(), f"missing authored document: {path}")
    return path.read_text()


def _h1(markdown: str, label: str) -> str:
    match = re.match(r"^#[ \t]+(.+?)[ \t]*$", markdown, re.M)
    _require(match is not None, f"{label} has no level-one heading")
    return match.group(1)


def _section(markdown: str, heading: str) -> str | None:
    match = re.search(
        rf"^## {re.escape(heading)}\s*$\n(.*?)(?=^## |\Z)", markdown, re.M | re.S
    )
    return match.group(1).strip() if match else None


def _plain(text: str) -> str:
    return re.sub(r"\s+", " ", text.replace("`", "").replace("\\", "")).strip()


def _heading_slugs(markdown: str) -> list[str]:
    return [
        re.sub(r"[^a-z0-9]+", "-", _plain(match.group(2)).lower()).strip("-")
        for match in HEADING.finditer(markdown)
    ]


def _check_links(markdown: str, source: Path, repo_root: Path) -> None:
    source = source if source.is_absolute() else repo_root / source
    for target in LINK.findall(markdown):
        if target.startswith(("#", "http://", "https://", "mailto:", "tel:")):
            continue
        _require(not target.startswith("//"), f"{source}: unsafe link {target!r}")
        path = target.split("#", 1)[0].split("?", 1)[0]
        _require(bool(path), f"{source}: empty link target")
        candidate = repo_root / path.lstrip("/") if path.startswith("/") else source.parent / path
        resolved = candidate.resolve()
        _require(
            resolved == repo_root.resolve() or repo_root.resolve() in resolved.parents,
            f"{source}: link escapes the repository: {target!r}",
        )
        _require(resolved.exists(), f"{source}: link target does not exist: {target!r}")


def canonical_handlers(repo_root: Path) -> dict[str, Path]:
    handlers: dict[str, Path] = {}
    for path in (repo_root / "kernel/src").glob("*/syscalls/**/*.cpp"):
        _require(path.stem not in handlers, f"duplicate syscall handler: {path}")
        handlers[path.stem] = path
    return handlers


def authored_source_path(name: str, subsystem: str, category: str) -> str:
    suffix = f"{category}/{name}.md" if subsystem == "proc" else f"{name}.md"
    return f"docs/2/{subsystem}/{suffix}"


def expected_proc_category(handler: Path, repo_root: Path) -> str:
    proc_root = repo_root / "kernel/src/proc/syscalls"
    _require(
        handler.resolve().is_relative_to(proc_root.resolve()),
        f"{handler}: proc syscall handler must live under {proc_root}",
    )
    parts = handler.resolve().relative_to(proc_root.resolve()).parts
    category = "core" if len(parts) == 1 else parts[0]
    _require(category in PROC_CATEGORIES, f"{handler}: unexpected proc category {category}")
    return category


def canonical_syscalls(abi_header: Path) -> list[tuple[int, str]]:
    entries: list[tuple[int, str]] = []
    for line in _read(abi_header).splitlines():
        match = re.match(r"\s*SYS_([A-Z0-9_]+)\s*=\s*(\d+)\s*,", line)
        if match:
            entries.append((int(match.group(2)), match.group(1).lower()))
    entries.sort()
    _require(
        [value for value, _ in entries] == list(CANONICAL_SYSCALL_IDS),
        f"{abi_header}: expected canonical syscall IDs 0..78, found {entries}",
    )
    _require(
        len({name for _, name in entries}) == len(entries),
        f"{abi_header}: duplicate canonical syscall names",
    )
    return entries


def _load_section2(repo_root: Path, docs_root: Path, manifest: dict[str, Any], abi_header: Path) -> tuple[dict[str, Any], ...]:
    section = manifest.get("section2")
    _require(isinstance(section, dict), "manifest.section2 must be an object")
    pages = section.get("pages")
    _require(isinstance(pages, list), "manifest.section2.pages must be a list")
    _require(len(pages) == len(CANONICAL_SYSCALL_IDS), "manifest must contain exactly 79 syscall pages")

    canonical = canonical_syscalls(abi_header)
    expected_names = {syscall_id: name for syscall_id, name in canonical}
    handlers = canonical_handlers(repo_root)
    records: list[dict[str, Any]] = []
    names: set[str] = set()
    slugs: set[str] = set()
    authored_paths: set[str] = set()
    for index, page in enumerate(pages):
        _require(isinstance(page, dict), f"section2 page {index} must be an object")
        syscall_id = page.get("id")
        _require(
            isinstance(syscall_id, int) and not isinstance(syscall_id, bool),
            f"section2 page {index} has no integer id",
        )
        _require(syscall_id in expected_names, f"unexpected syscall id {syscall_id}")
        name = page.get("name")
        _require(name == expected_names[syscall_id], f"syscall {syscall_id} must be named {expected_names[syscall_id]}")
        _require(name not in names, f"duplicate syscall name {name}")
        names.add(name)

        slug = page.get("slug")
        expected_slug = f"syscall-{syscall_id}-{name.replace('_', '-')}"
        _require(slug == expected_slug, f"{name}: slug must be {expected_slug}")
        _require(slug not in slugs, f"duplicate syscall slug {slug}")
        slugs.add(slug)

        _require(page.get("title") == f"{name}(2)", f"{name}: title must be {name}(2)")
        subsystem = page.get("subsystem")
        _require(subsystem in SECTION2_SUBSYSTEMS, f"{name}: invalid subsystem")
        category = page.get("category")
        _require(isinstance(category, str) and bool(CATEGORY.fullmatch(category)), f"{name}: invalid category")
        handler = handlers.get(name)
        _require(handler is not None, f"{name}: no canonical syscall handler")
        assert handler is not None
        if subsystem == "proc":
            _require(category == expected_proc_category(handler, repo_root), f"{name}: proc category does not match its handler")
        elif subsystem == "dl":
            _require(category == "dl", f"{name}: dl category must be dl")
            _require(
                handler.resolve().is_relative_to((repo_root / "kernel/src/dl/syscalls").resolve()),
                f"{handler}: dl syscall handler must live under kernel/src/dl/syscalls",
            )
        elif subsystem == "fs":
            _require(category == "fs", f"{name}: fs category must be fs")
            _require(
                handler.resolve().is_relative_to((repo_root / "kernel/src/fs/syscalls").resolve()),
                f"{handler}: fs syscall handler must live under kernel/src/fs/syscalls",
            )
        elif subsystem == "mod":
            _require(category == "mod", f"{name}: mod category must be mod")
            _require(
                handler.resolve().is_relative_to((repo_root / "kernel/src/mod/syscalls").resolve()),
                f"{handler}: mod syscall handler must live under kernel/src/mod/syscalls",
            )
        else:
            _require(category == "abi", f"{name}: abi category must be abi")

        source_path = page.get("sourcePath")
        _require(isinstance(source_path, str) and bool(source_path), f"{name}: sourcePath is missing")
        expected_source_path = authored_source_path(name, subsystem, category)
        _require(source_path == expected_source_path, f"{name}: sourcePath must be {expected_source_path}")
        _require(source_path not in authored_paths, f"{name}: duplicate sourcePath {source_path}")
        authored_paths.add(source_path)

        source = _safe_relative(page.get("source"), f"{name}.source")
        _require((repo_root / source).is_file(), f"{name}: source does not exist: {source}")

        kind = page.get("synopsisKind")
        _require(kind in {"public", "raw"}, f"{name}: invalid synopsis kind")
        _require(
            (name == "sigreturn") == (kind == "raw"),
            "sigreturn is the only raw syscall page",
        )

        document = _safe_relative(source_path, f"{name}.markdown")
        _require(document.parts[:2] == ("docs", "2"), f"{name}: sourcePath must live under docs/2")
        markdown = _read(repo_root / document)
        _require(_h1(markdown, name) == f"{name}(2)", f"{name}: authored heading must be {name}(2)")
        synopsis = _section(markdown, "SYNOPSIS")
        _require(synopsis is not None, f"{name}: authored page has no SYNOPSIS")
        declared = page.get("synopsis")
        _require(isinstance(declared, str) and bool(declared), f"{name}: manifest synopsis is missing")
        if kind == "raw":
            _require(declared == RAW_SYNOPSIS, f"{name}: raw synopsis must be the raw ABI notice")
            _require(synopsis == RAW_SYNOPSIS, f"{name}: authored raw synopsis does not match")
        else:
            block = FENCE.fullmatch(synopsis)
            _require(block is not None, f"{name}: public synopsis must be one c block")
            _require(block.group(1).strip() == declared, f"{name}: authored and declared synopsis differ")
        _require(_section(markdown, "DESCRIPTION") is not None, f"{name}: authored page has no DESCRIPTION")
        _check_links(markdown, document, repo_root)
        records.append(dict(page, path=document.as_posix(), markdown=markdown))

    _require(names == set(expected_names.values()), "section2 names do not match the ABI")
    expected_documents = {(repo_root / source_path).resolve() for source_path in authored_paths}
    _require(
        expected_documents == {path.resolve() for path in (docs_root / "2").rglob("*.md")},
        "docs/2 must contain exactly one Markdown page per canonical syscall",
    )
    _require(
        not list((docs_root / "2").glob("*.md")),
        "docs/2 must not contain flat Markdown pages",
    )
    records.sort(key=lambda record: record["id"])
    _require(
        [record["id"] for record in records] == list(CANONICAL_SYSCALL_IDS),
        "section2 pages must be ordered 0..78",
    )
    return tuple(records)


def _load_section7(repo_root: Path, docs_root: Path, manifest: dict[str, Any], section2_slugs: set[str]) -> tuple[dict[str, Any], ...]:
    section = manifest.get("section7")
    _require(isinstance(section, dict), "manifest.section7 must be an object")
    pages = section.get("pages")
    _require(isinstance(pages, list), "manifest.section7.pages must be a list")
    _require(len(pages) == len(SECTION7_NAMES), "manifest must contain exactly 7 overview pages")

    records: list[dict[str, Any]] = []
    names: set[str] = set()
    slugs = set(section2_slugs)
    for page in pages:
        _require(isinstance(page, dict), "section7 page must be an object")
        name = page.get("name")
        _require(name in SECTION7_NAMES, f"unexpected section7 name {name!r}")
        _require(name not in names, f"duplicate section7 name {name}")
        names.add(name)
        _require(page.get("slug") == SECTION7_SLUGS[name], f"{name}: stable slug changed")
        _require(page.get("slug") not in slugs, f"{name}: duplicate route slug")
        slugs.add(page["slug"])
        aliases = page.get("aliases")
        _require(isinstance(aliases, list) and aliases == [SECTION7_ALIASES[name]], f"{name}: explicit alias missing")
        for alias in aliases:
            _require(alias not in slugs, f"{name}: duplicate route alias {alias}")
            slugs.add(alias)
        _require(bool(page.get("title")), f"{name}: missing title")
        source = _safe_relative(page.get("source"), f"{name}.source")
        expected_source = (docs_root / "7" / f"{name}.md").relative_to(repo_root)
        _require(source == expected_source, f"{name}: source must be {expected_source}")
        markdown = _read(repo_root / source)
        _require(_plain(_h1(markdown, name)) == _plain(page["title"]), f"{name}: title and heading differ")
        headings = _heading_slugs(markdown)
        _require(len(headings) == len(set(headings)), f"{name}: duplicate heading anchors")
        _check_links(markdown, source, repo_root)
        records.append(dict(page, path=source.as_posix(), markdown=markdown))
    _require(names == set(SECTION7_NAMES), "section7 names are incomplete")
    _require(
        {(docs_root / "7" / f"{name}.md").resolve() for name in names}
        == {path.resolve() for path in (docs_root / "7").glob("*.md")},
        "docs/7 must contain exactly the seven overview pages",
    )
    return tuple(records)


def load_authored_docs(repo_root: Path, docs_root: Path | None = None) -> AuthoredDocs:
    repo_root = repo_root.resolve()
    docs_root = (docs_root or repo_root / "docs").resolve()
    manifest_path = docs_root / "manifest.json"
    _require(manifest_path.is_file(), f"missing documentation manifest: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text())
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise DocsValidationError(f"invalid documentation manifest: {error}") from error
    _require(isinstance(manifest, dict), "documentation manifest must be an object")
    _require("content" not in json.dumps(manifest), "manifest must not contain Markdown bodies")

    section2 = _load_section2(
        repo_root,
        docs_root,
        manifest,
        repo_root / "kernel/include/astia/api/syscalls.h",
    )
    section7 = _load_section7(
        repo_root, docs_root, manifest, {record["slug"] for record in section2}
    )
    return AuthoredDocs(section2=section2, section7=section7)
