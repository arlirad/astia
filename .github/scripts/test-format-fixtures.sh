#!/usr/bin/env bash
set -Eeuo pipefail

die() {
    printf 'clang-format fixture check: error: %s\n' "$*" >&2
    exit 2
}

repo_root="$(git rev-parse --show-toplevel)" ||
    die 'not inside a Git repository'
cd "$repo_root"

formatter="${CLANG_FORMAT_BIN:-clang-format}"
if ! command -v "$formatter" >/dev/null 2>&1; then
    die "formatter '$formatter' is not installed"
fi

fixture_dir="$repo_root/.github/fixtures/clang-format"
input="$fixture_dir/include-categories.input"
expected="$fixture_dir/include-categories.expected"
[[ -r "$input" ]] || die "fixture input '$input' is missing or unreadable"
[[ -r "$expected" ]] || die "fixture output '$expected' is missing or unreadable"

actual="$(mktemp)"
trap 'rm -f "$actual"' EXIT
"$formatter" \
    --style="file:$repo_root/.clang-format" \
    --assume-filename=include-categories.cpp \
    "$input" >"$actual" ||
    die 'formatter failed for include category fixture'

if ! diff -u "$expected" "$actual"; then
    die 'include category fixture output differs from the expected result'
fi

printf 'clang-format fixture check: passed\n'
