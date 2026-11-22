#!/usr/bin/env python3

import pathlib


WORKFLOW = pathlib.Path(__file__).resolve().parent.parent / "workflows/clang-format.yml"


def main():
    workflow = WORKFLOW.read_text(encoding="utf-8")
    assert "pull_request_target" not in workflow
    assert "pull_request:" in workflow
    assert "merge_group:" in workflow
    assert "push:\n    branches:\n      - main\n" in workflow
    assert '"**"' not in workflow
    assert (
        "name: ${{ (github.event_name == 'pull_request' "
        "|| github.event_name == 'merge_group') && 'clang-format-pr' "
        "|| 'clang-format-baseline' }}"
    ) in workflow
    assert "github.event.merge_group.base_sha" in workflow
    assert "permissions:\n  contents: read" in workflow
    assert ".github/scripts/check-format.sh" in workflow
    assert ".github/scripts/test-check-format.sh" in workflow
    print("clang-format workflow test: passed")


if __name__ == "__main__":
    main()
