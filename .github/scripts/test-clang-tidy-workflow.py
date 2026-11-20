#!/usr/bin/env python3

import pathlib


WORKFLOW = pathlib.Path(__file__).resolve().parent.parent / "workflows/clang-tidy.yml"


def main():
    workflow = WORKFLOW.read_text(encoding="utf-8")
    assert "pull_request_target" not in workflow
    assert "pull_request:" in workflow
    assert "permissions:\n  contents: read" in workflow
    assert "persist-credentials: false" in workflow
    assert 'git show "${BASE_SHA}:.github/scripts/check-clang-tidy.py"' in workflow
    assert 'git show "${BASE_SHA}:.github/scripts/clang-tidy-config.yml"' in workflow
    assert 'test -s "$RUNNER_TEMP/clang-tidy-trusted/check-clang-tidy.py"' in workflow
    assert 'test -s "$RUNNER_TEMP/clang-tidy-trusted/clang-tidy-config.yml"' in workflow
    assert "workflow_dispatch:" not in workflow
    print("clang-tidy workflow test: passed")


if __name__ == "__main__":
    main()
