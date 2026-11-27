#!/usr/bin/env python3

import argparse
import concurrent.futures
import json
import pathlib
import re
import subprocess
import sys
import tempfile


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
COMPILE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
UNSUPPORTED_PREFIXES = ("kernel/src/arch/aarch64/",)
UNSUPPORTED_FILES = {
    "kernel/src/test/selftest_aarch64.cpp",
    "kernel/tests/public_header_alone.cpp",
    "kernel/tests/pipe_api_test.cpp",
    "kernel/tests/module_symbols.cpp",
    "kernel/tests/module_priority_overflow_fixture.cpp",
    "kernel/tests/module_priority_malformed_fixture.cpp",
    "kernel/tests/module_priority_init_trailing_dot_fixture.cpp",
    "kernel/tests/module_priority_fini_trailing_dot_fixture.cpp",
    "kernel/tests/module_lifecycle_priority_fixture.cpp",
    "kernel/tests/module_lifecycle_no_fini_fixture.cpp",
    "kernel/tests/module_lifecycle_limit_fixture.cpp",
    "kernel/tests/module_lifecycle_fixture.cpp",
    "kernel/tests/module_lifecycle_dso_fixture.cpp",
    "kernel/tests/module_lifecycle_atexit_fixture.cpp",
    "kernel/tests/init_user.c",
    "kernel/tests/dyn_libc.c",
    "kernel/tests/fixture_base.c",
    "kernel/tests/fixture_lib.c",
    "kernel/tests/init_libc.c",
    "kernel/tests/interp_link.c",
    "kernel/tests/fixture_ifunc.c",
    "kernel/tests/interp_ifunc.c",
    "kernel/tests/interp_relro.c",
    "kernel/tests/fixture_dynamic_tls.c",
    "kernel/tests/interp_dyntls.c",
    "kernel/tests/fixture_exec.c",
    "kernel/tests/support/file_host.cpp",
}
CONFIG = pathlib.Path(__file__).with_name("clang-tidy-config.yml").resolve()
HEADER_FILTER = r"(^|/)kernel/(include|src|tests)/"
SKIPPED_COMPILE_COMMAND = re.compile(
    r"(?ims)^Skipping\s+.+?(?:\s*[:.]\s*|\s+)"
    r"Compile\s+command\s+not\s+found\s*\.\s*$"
)


def git(args, root):
    return subprocess.check_output(["git", *args], cwd=root)


def decode_path(raw):
    text = raw.decode("utf-8", "surrogateescape") if isinstance(raw, bytes) else raw
    if not (text.startswith('"') and text.endswith('"')):
        return text
    data = bytearray()
    escapes = {
        "a": 7,
        "b": 8,
        "t": 9,
        "n": 10,
        "v": 11,
        "f": 12,
        "r": 13,
        "\\": 92,
        '"': 34,
    }
    i = 1
    while i < len(text) - 1:
        char = text[i]
        if char != "\\":
            data.extend(char.encode("utf-8", "surrogateescape"))
            i += 1
        else:
            i += 1
            if i >= len(text) - 1:
                raise ValueError("invalid quoted path")
            char = text[i]
            if char in escapes:
                data.append(escapes[char])
                i += 1
            elif char in "01234567":
                match = re.match(r"[0-7]{1,3}", text[i:])
                data.append(int(match.group(0), 8))
                i += len(match.group(0))
            else:
                raise ValueError(f"invalid path escape: {char!r}")
    return data.decode("utf-8", "surrogateescape")


def status_entries(data):
    fields = data.split(b"\0")
    entries = []
    i = 0
    while i < len(fields) and fields[i]:
        status = fields[i].decode("ascii")
        i += 1
        if status[0] in "RC":
            if i + 1 >= len(fields):
                raise ValueError("truncated rename status")
            i += 1
        if i >= len(fields):
            raise ValueError("truncated status output")
        entries.append((status, decode_path(fields[i])))
        i += 1
    return entries


def changed_ranges(diff):
    result = {}
    path = None
    in_hunks = False
    expect_header = False
    for line in diff.splitlines():
        if line.startswith("diff --git "):
            path = None
            in_hunks = False
            expect_header = False
            continue
        if in_hunks:
            match = re.match(r"^@@ .* \+(\d+)(?:,(\d+))?", line)
            if match:
                start, count = int(match[1]), int(match[2] or 1)
                if count:
                    result[path].append([start, start + count - 1])
            continue
        if line.startswith("--- "):
            expect_header = True
            continue
        if expect_header and line.startswith("+++ "):
            raw_path = line[4:]
            if raw_path.endswith("\t"):
                raw_path = raw_path[:-1]
            raw_path = decode_path(raw_path)
            path = raw_path[2:] if raw_path.startswith(("a/", "b/")) else raw_path
            if pathlib.PurePosixPath(path).suffix.lower() not in SOURCE_SUFFIXES:
                path = None
            elif path not in result:
                result[path] = []
            expect_header = False
            continue
        if path and line.startswith("@@ "):
            in_hunks = True
            match = re.match(r"^@@ .* \+(\d+)(?:,(\d+))?", line)
            if match:
                start, count = int(match[1]), int(match[2] or 1)
                if count:
                    result[path].append([start, start + count - 1])
    return result


def compile_db(build, root):
    with (pathlib.Path(build) / "compile_commands.json").open(
        encoding="utf-8"
    ) as stream:
        entries = json.load(stream)
    root = pathlib.Path(root).resolve()
    kernel = (pathlib.Path(root) / "kernel").resolve()
    result = {}
    normalized = []
    for entry in entries:
        directory = pathlib.Path(entry.get("directory", root))
        if not directory.is_absolute():
            directory = root / directory
        directory = directory.resolve()
        file = pathlib.Path(entry["file"])
        file = (
            (directory / file).resolve() if not file.is_absolute() else file.resolve()
        )
        if file.suffix.lower() not in COMPILE_SUFFIXES or not file.is_file():
            continue
        try:
            file.relative_to(kernel)
        except ValueError:
            continue
        normalized_entry = dict(entry)
        normalized_entry["directory"] = str(directory)
        normalized_entry["file"] = str(file)
        normalized.append(normalized_entry)
        result[str(file)] = str(file)
    normalized_build = tempfile.TemporaryDirectory(prefix="clang-tidy-db-")
    (pathlib.Path(normalized_build.name) / "compile_commands.json").write_text(
        json.dumps(normalized), encoding="utf-8"
    )
    return result, normalized_build


def line_filter_entries(root, path, lines):
    absolute = str((pathlib.Path(root) / path).resolve())
    return [
        {"name": path, "lines": lines},
        {"name": absolute, "lines": lines},
    ]


def analyze(args, source, line_filter):
    command = [
        args.clang_tidy,
        f"-line-filter={json.dumps(line_filter, separators=(',', ':'))}",
        f"-config-file={CONFIG}",
        f"-header-filter={HEADER_FILTER}",
        f"-p={args.build}",
        source,
    ]
    try:
        result = subprocess.run(
            command,
            cwd=args.root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
            timeout=args.timeout,
        )
    except (OSError, UnicodeError, subprocess.TimeoutExpired) as error:
        return source, 1, f"clang-tidy invocation failed for {source}: {error}\n"
    output = result.stdout + result.stderr
    if result.returncode == 0 and SKIPPED_COMPILE_COMMAND.search(output):
        return source, 1, output
    return source, result.returncode, output


def filter_chunks(line_filter, budget):
    """Split the line filter so each invocation stays under the argument limit.

    Linux rejects a single argv entry larger than MAX_ARG_STRLEN (128 KiB), and
    a whitespace-wide change can produce a filter well above that. Each chunk is
    a complete, valid filter; running a translation unit once per chunk yields
    the same diagnostics as one run with the unsplit filter.
    """
    chunks = []
    current = []
    size = 2
    for entry in line_filter:
        encoded = len(json.dumps(entry, separators=(",", ":")).encode("utf-8")) + 1
        if current and size + encoded > budget:
            chunks.append(current)
            current = []
            size = 2
        current.append(entry)
        size += encoded
    if current or not chunks:
        chunks.append(current)
    return chunks


def run_units(args, units, line_filter):
    failure = False
    chunks = filter_chunks(line_filter, args.line_filter_budget)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [
            pool.submit(analyze, args, unit, chunk)
            for chunk in chunks
            for unit in units
        ]
        for future in concurrent.futures.as_completed(futures):
            source, code, output = future.result()
            if output.strip():
                print(f"===== {source} =====\n{output}", end="")
            failure |= code != 0
    return int(failure)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--line-filter-budget", type=int, default=90000)
    args = parser.parse_args()
    args.root = str(pathlib.Path(args.root).resolve())
    if not pathlib.Path(args.build).is_absolute():
        args.build = str((pathlib.Path(args.root) / args.build).resolve())
    if (
        args.jobs < 1
        or args.timeout < 1
        or args.line_filter_budget < 256
        or not CONFIG.is_file()
    ):
        print("clang-tidy: invalid settings or missing trusted config", file=sys.stderr)
        return 1
    normalized_build = None
    try:
        status = status_entries(
            git(
                [
                    "diff",
                    "--name-status",
                    "-z",
                    "--diff-filter=ACMRT",
                    args.base,
                    args.head,
                    "--",
                    "kernel",
                ],
                args.root,
            )
        )
        diff = git(
            [
                "diff",
                "--unified=0",
                "--no-color",
                "--no-ext-diff",
                args.base,
                args.head,
                "--",
                "kernel",
            ],
            args.root,
        ).decode("utf-8", "strict")
        ranges = changed_ranges(diff)
        db, normalized_build = compile_db(args.build, args.root)
        args.build = normalized_build.name
    except (
        OSError,
        UnicodeError,
        ValueError,
        TypeError,
        KeyError,
        json.JSONDecodeError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"clang-tidy: failed to prepare analysis: {error}", file=sys.stderr)
        return 1
    relevant = [
        (status, path)
        for status, path in status
        if pathlib.PurePosixPath(path).suffix.lower() in SOURCE_SUFFIXES
    ]
    if not relevant:
        smoke = str((pathlib.Path(args.root) / "kernel/src/kmain.cpp").resolve())
        if smoke not in db:
            print(
                "clang-tidy: smoke source missing from compilation database",
                file=sys.stderr,
            )
            return 1
        lines = len(pathlib.Path(smoke).read_text(encoding="utf-8").splitlines())
        print("clang-tidy: empty diff; running smoke analysis")
        return run_units(
            args,
            [smoke],
            line_filter_entries(
                args.root, "kernel/src/kmain.cpp", [[1, max(lines, 1)]]
            ),
        )
    sources = []
    missing_sources = []
    unanalyzable_renames = []
    headers = []
    for entry_status, path in relevant:
        if path not in ranges or not ranges[path]:
            if entry_status == "R100":
                unanalyzable_renames.append(path)
            continue
        if pathlib.PurePosixPath(path).suffix.lower() not in COMPILE_SUFFIXES:
            headers.append(path)
            continue
        absolute = str((pathlib.Path(args.root) / path).resolve())
        if absolute in db:
            sources.append((path, absolute))
        elif path.startswith(UNSUPPORTED_PREFIXES) or path in UNSUPPORTED_FILES:
            print(f"clang-tidy: explicitly unsupported source: {path}")
        else:
            missing_sources.append(path)
    if unanalyzable_renames:
        print(
            f"clang-tidy: no changed ranges for {unanalyzable_renames!r}",
            file=sys.stderr,
        )
        return 1
    if missing_sources:
        print(
            f"clang-tidy: missing compile database entries: {missing_sources!r}",
            file=sys.stderr,
        )
        return 1
    failed = 0
    if sources:
        filt = [
            entry
            for path, _ in sources
            for entry in line_filter_entries(args.root, path, ranges[path])
        ]
        failed |= run_units(args, [source for _, source in sources], filt)
    if headers:
        if not db:
            print(
                "clang-tidy: no translation units for changed headers", file=sys.stderr
            )
            return 1
        filt = [
            entry
            for path in headers
            for entry in line_filter_entries(args.root, path, ranges[path])
        ]
        print(f"clang-tidy: checking {len(db)} translation units for changed headers")
        failed |= run_units(args, list(db.values()), filt)
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
