#!/usr/bin/env python3

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


SCRIPTS = pathlib.Path(__file__).resolve().parent


def run(command, cwd, env=None, check=True):
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=check,
    )


def git(repo, *args):
    return run(["git", *args], repo).stdout.strip()


def commit(repo, message):
    run(["git", "add", "-A"], repo)
    run(["git", "commit", "-m", message], repo)
    return git(repo, "rev-parse", "HEAD")


def repo_fixture():
    repo = pathlib.Path(tempfile.mkdtemp(prefix="clang-tidy-fixture-"))
    run(["git", "init", "--quiet", "--initial-branch=main", str(repo)], repo.parent)
    git(repo, "config", "user.email", "test@example.invalid")
    git(repo, "config", "user.name", "fixture")
    (repo / "kernel/src").mkdir(parents=True)
    (repo / "kernel/include").mkdir(parents=True)
    return repo


def database(repo, files):
    build = repo / "build"
    build.mkdir(exist_ok=True)
    entries = []
    for name in files:
        entries.append(
            {
                "directory": str(repo / "kernel/src"),
                "file": pathlib.Path(name).name,
                "command": f"cc -c {pathlib.Path(name).name}",
            }
        )
    (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
    return build


def fake_tidy(repo):
    path = repo / "fake-tidy"
    path.write_text(
        """#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
with open(os.environ["TIDY_RECORD"], "a", encoding="utf-8") as f:
    json.dump(args, f); f.write("\\n")
line_filter = next(a for a in args if a.startswith("-line-filter="))
if os.environ.get("FAIL_HEADER") == "1" and "header.hpp" in line_filter:
    print("kernel/include/header.hpp:1: error: injected header finding")
    raise SystemExit(1)
if os.environ.get("INVALID_OUTPUT") == "1":
    sys.stdout.buffer.write(b"\\xff")
if os.environ.get("SKIP_OUTPUT") == "1":
    print("Skipping /path/file.cpp. Compile command not found.")
""",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def check(repo, build, base, head, tidy, flags=None, cwd=None, extra=None):
    env = os.environ.copy()
    env["TIDY_RECORD"] = str(repo / "record")
    env.update(flags or {})
    return run(
        [
            sys.executable,
            str(SCRIPTS / "check-clang-tidy.py"),
            *(extra or []),
            "--root",
            str(repo),
            "--build",
            str(build),
            "--base",
            base,
            "--head",
            head,
            "--clang-tidy",
            str(tidy),
            "--jobs",
            "2",
            "--timeout",
            "10",
        ],
        cwd or repo,
        env=env,
        check=False,
    )


def test_special_paths_and_hunk_content():
    repo = repo_fixture()
    names = [
        "λ.cpp",
        "space name.cpp",
        "tab\tfile.cpp",
        "line\nbreak.cpp",
        "back\\slash.cpp",
        'quote".cpp',
    ]
    for index, name in enumerate(names):
        (repo / "kernel/src" / name).write_text(f"int old_{index};\n", encoding="utf-8")
    base = commit(repo, "base")
    for index, name in enumerate(names):
        with (repo / "kernel/src" / name).open("a", encoding="utf-8") as stream:
            stream.write(f"int new_{index};\n")
            if index == 0:
                stream.write("++ b;\n")
    head = commit(repo, "special paths")
    build = database(repo, [f"kernel/src/{name}" for name in names])
    result = check(repo, build, base, head, fake_tidy(repo))
    assert result.returncode == 0, result.stdout
    invocations = [
        json.loads(line) for line in (repo / "record").read_text().splitlines()
    ]
    assert len(invocations) == len(names), invocations
    filters = [
        json.loads(
            next(
                x.split("=", 1)[1] for x in invocation if x.startswith("-line-filter=")
            )
        )
        for invocation in invocations
    ]
    assert any(
        item["name"] == "kernel/src/λ.cpp" and item["lines"] == [[2, 3]]
        for item in filters[0]
    ), filters
    assert any(
        item["name"] == str((repo / "kernel/src/λ.cpp").resolve())
        and item["lines"] == [[2, 3]]
        for item in filters[0]
    ), filters


def test_mixed_header_source_fails_on_header():
    repo = repo_fixture()
    source = repo / "kernel/src/main.cpp"
    other = repo / "kernel/src/other.cpp"
    header = repo / "kernel/include/header.hpp"
    source.write_text('#include "header.hpp"\nint x;\n', encoding="utf-8")
    other.write_text("int y;\n", encoding="utf-8")
    header.write_text("#define VALUE 1\n", encoding="utf-8")
    base = commit(repo, "base")
    source.write_text('#include "header.hpp"\nint x;\nint added;\n', encoding="utf-8")
    other.write_text("int y;\nint unrelated;\n", encoding="utf-8")
    header.write_text("#define VALUE 2\n", encoding="utf-8")
    head = commit(repo, "mixed")
    result = check(
        repo,
        database(repo, ["kernel/src/main.cpp", "kernel/src/other.cpp"]),
        base,
        head,
        fake_tidy(repo),
        {"FAIL_HEADER": "1"},
    )
    assert result.returncode == 1 and "injected header finding" in result.stdout, (
        result.stdout
    )
    invocations = [
        json.loads(line) for line in (repo / "record").read_text().splitlines()
    ]
    assert any(
        item["name"] == str(header.resolve())
        for invocation in invocations
        for item in json.loads(
            next(
                value.split("=", 1)[1]
                for value in invocation
                if value.startswith("-line-filter=")
            )
        )
    ), invocations


def test_missing_compile_entry_fails():
    repo = repo_fixture()
    src = repo / "kernel/src/missing.cpp"
    src.write_text("int x;\n", encoding="utf-8")
    base = commit(repo, "base")
    src.write_text("int x;\nint y;\n", encoding="utf-8")
    head = commit(repo, "change")
    result = check(repo, database(repo, []), base, head, fake_tidy(repo))
    assert result.returncode == 1 and "missing compile database" in result.stdout, (
        result.stdout
    )


def test_relative_compile_database_directory():
    repo = repo_fixture()
    source = repo / "kernel/src/main.cpp"
    source.write_text("int x;\n", encoding="utf-8")
    base = commit(repo, "base")
    source.write_text("int x;\nint y;\n", encoding="utf-8")
    head = commit(repo, "change")
    build = repo / "build"
    build.mkdir()
    (build / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": "kernel",
                    "file": "src/main.cpp",
                    "command": "cc -c src/main.cpp",
                }
            ]
        ),
        encoding="utf-8",
    )
    tidy = fake_tidy(repo)
    result = check(repo, build, base, head, tidy, cwd=repo.parent)
    assert result.returncode == 0, result.stdout
    invocation = json.loads((repo / "record").read_text().splitlines()[0])
    assert str(source.resolve()) in invocation, invocation
    line_filter = json.loads(
        next(
            value.split("=", 1)[1]
            for value in invocation
            if value.startswith("-line-filter=")
        )
    )
    assert line_filter == [
        {"name": "kernel/src/main.cpp", "lines": [[2, 2]]},
        {"name": str(source.resolve()), "lines": [[2, 2]]},
    ], line_filter


def test_type_change_analyzed_and_deletion_only_skipped():
    repo = repo_fixture()
    real = repo / "kernel/src/real.cpp"
    source = repo / "kernel/src/type.cpp"
    real.write_text("int x;\n", encoding="utf-8")
    source.symlink_to("real.cpp")
    base = commit(repo, "symlink")
    source.unlink()
    source.write_text("int regular;\nint added;\n", encoding="utf-8")
    head = commit(repo, "regular")
    assert git(repo, "diff", "--name-status", base, head).startswith("T\t")
    result = check(
        repo, database(repo, ["kernel/src/type.cpp"]), base, head, fake_tidy(repo)
    )
    assert result.returncode == 0, result.stdout

    removed = repo / "kernel/src/removed.cpp"
    removed.write_text("int first;\nint second;\n", encoding="utf-8")
    before_removal = commit(repo, "before removal")
    removed.write_text("", encoding="utf-8")
    deletion = commit(repo, "deletion only")
    assert git(repo, "diff", "--name-status", before_removal, deletion).startswith(
        "M\t"
    )
    record = repo / "record"
    if record.exists():
        record.unlink()
    result = check(
        repo,
        database(repo, ["kernel/src/removed.cpp"]),
        before_removal,
        deletion,
        fake_tidy(repo),
    )
    assert result.returncode == 0, result.stdout
    assert not record.exists(), result.stdout


def test_rename_only_fails_closed_and_modified_rename_analyzed():
    repo = repo_fixture()
    old = repo / "kernel/src/old.cpp"
    new = repo / "kernel/src/renamed.cpp"
    old.write_text("".join(f"int value{i};\n" for i in range(10)), encoding="utf-8")
    base = commit(repo, "base")
    run(["git", "mv", str(old), str(new)], repo)
    renamed = commit(repo, "rename only")
    assert git(repo, "diff", "--name-status", base, renamed).startswith("R100\t")
    result = check(
        repo, database(repo, ["kernel/src/renamed.cpp"]), base, renamed, fake_tidy(repo)
    )
    assert result.returncode == 1 and "no changed ranges" in result.stdout, (
        result.stdout
    )

    new.write_text(
        "".join(f"int value{i};\n" for i in range(10)) + "int added;\n",
        encoding="utf-8",
    )
    edited = commit(repo, "rename with edit")
    assert git(
        repo, "diff", "--name-status", base, edited, "--", "kernel/src"
    ).startswith("R")
    record = repo / "record"
    if record.exists():
        record.unlink()
    result = check(
        repo, database(repo, ["kernel/src/renamed.cpp"]), base, edited, fake_tidy(repo)
    )
    assert result.returncode == 0, result.stdout
    invocation = json.loads(record.read_text().splitlines()[0])
    line_filter = json.loads(
        next(
            value.split("=", 1)[1]
            for value in invocation
            if value.startswith("-line-filter=")
        )
    )
    assert line_filter == [
        {"name": "kernel/src/renamed.cpp", "lines": [[11, 11]]},
        {
            "name": str((repo / "kernel/src/renamed.cpp").resolve()),
            "lines": [[11, 11]],
        },
    ], line_filter


def test_empty_diff_smoke_and_errors():
    repo = repo_fixture()
    source = repo / "kernel/src/kmain.cpp"
    source.write_text("int x;\n", encoding="utf-8")
    head = commit(repo, "base")
    build = database(repo, ["kernel/src/kmain.cpp"])
    tidy = fake_tidy(repo)
    result = check(repo, build, head, head, tidy)
    assert result.returncode == 0 and "smoke analysis" in result.stdout, result.stdout
    result = check(repo, build, head, head, repo / "missing-tidy")
    assert result.returncode == 1, result.stdout
    result = check(repo, build, head, head, tidy, {"INVALID_OUTPUT": "1"})
    assert result.returncode == 1, result.stdout
    result = check(repo, build, head, head, tidy, {"SKIP_OUTPUT": "1"})
    assert result.returncode == 1, result.stdout


def test_real_clang_tidy_finding_with_relative_database():
    tidy = shutil.which(os.environ.get("CLANG_TIDY", "clang-tidy"))
    if not tidy:
        print("real clang-tidy integration test: skipped")
        return
    repo = repo_fixture()
    source = repo / "kernel/src/main.cpp"
    source.write_text(
        "int divide(int value) {\n    return value ? value : 1;\n}\n",
        encoding="utf-8",
    )
    base = commit(repo, "base")
    source.write_text(
        "int divide(int value) {\n"
        "    int divisor = value - value;\n"
        "    return value / divisor;\n"
        "}\n",
        encoding="utf-8",
    )
    head = commit(repo, "division finding")
    build = repo / "build"
    build.mkdir()
    (build / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": "kernel",
                    "file": "src/main.cpp",
                    "command": f"clang++ -std=c++20 -c {source}",
                }
            ]
        ),
        encoding="utf-8",
    )
    result = check(repo, build, base, head, tidy)
    assert result.returncode == 1, result.stdout
    assert "Division by zero" in result.stdout, result.stdout


def test_base_trusted_config_ignores_pr_config():
    repo = repo_fixture()
    source = repo / "kernel/src/kmain.cpp"
    source.write_text("int x;\n", encoding="utf-8")
    base = commit(repo, "base")
    (repo / ".clang-tidy").write_text("Checks: '-*'\n", encoding="utf-8")
    head = commit(repo, "malicious config")
    result = check(
        repo, database(repo, ["kernel/src/kmain.cpp"]), base, head, fake_tidy(repo)
    )
    args = json.loads((repo / "record").read_text().splitlines()[0])
    config = next(x for x in args if x.startswith("-config-file="))
    assert result.returncode == 0 and config.endswith("clang-tidy-config.yml"), (
        result.stdout
    )
    assert ".clang-tidy" not in config


def test_split_line_filter_under_argument_limit():
    repo = repo_fixture()
    names = ["kernel/src/alpha.cpp", "kernel/src/beta.cpp", "kernel/src/gamma.cpp"]
    for name in names:
        (repo / name).write_text("int base_value;\n", encoding="utf-8")
    base = commit(repo, "base")
    for name in names:
        with (repo / name).open("a", encoding="utf-8") as stream:
            stream.write("int added_value;\n")
    head = commit(repo, "whitespace-wide change")
    result = check(
        repo,
        database(repo, names),
        base,
        head,
        fake_tidy(repo),
        extra=["--line-filter-budget", "300"],
    )
    assert result.returncode == 0, result.stdout
    invocations = [
        json.loads(line) for line in (repo / "record").read_text().splitlines()
    ]
    assert len(invocations) > len(names), invocations
    by_source = {}
    for invocation in invocations:
        source = invocation[-1]
        raw = next(
            value.split("=", 1)[1]
            for value in invocation
            if value.startswith("-line-filter=")
        )
        assert len(raw.encode()) <= 300, (source, len(raw.encode()), raw)
        chunk = json.loads(raw)
        assert chunk, invocation
        by_source.setdefault(source, []).append(chunk)
    for source in by_source:
        assert len(by_source[source]) > 1, (source, by_source[source])
        union = [entry for chunk in by_source[source] for entry in chunk]
        expected = [
            entry
            for name in names
            for entry in (
                {"name": name, "lines": [[2, 2]]},
                {"name": str((repo / name).resolve()), "lines": [[2, 2]]},
            )
        ]
        assert sorted(
            union, key=lambda entry: (entry["name"], entry["lines"])
        ) == sorted(expected, key=lambda entry: (entry["name"], entry["lines"])), (
            source,
            union,
        )


def main():
    test_special_paths_and_hunk_content()
    test_mixed_header_source_fails_on_header()
    test_missing_compile_entry_fails()
    test_relative_compile_database_directory()
    test_type_change_analyzed_and_deletion_only_skipped()
    test_rename_only_fails_closed_and_modified_rename_analyzed()
    test_empty_diff_smoke_and_errors()
    test_real_clang_tidy_finding_with_relative_database()
    test_base_trusted_config_ignores_pr_config()
    test_split_line_filter_under_argument_limit()
    print("clang-tidy checker tests: passed")


if __name__ == "__main__":
    main()
