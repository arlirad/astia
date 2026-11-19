#!/usr/bin/env bash
set -Eeuo pipefail

die() {
    printf 'clang-format check: error: %s\n' "$*" >&2
    exit 2
}

repo_root="$(git rev-parse --show-toplevel)" ||
    die 'not inside a Git repository'
cd "$repo_root"

formatter="${CLANG_FORMAT_BIN:-clang-format}"
if ! command -v "$formatter" >/dev/null 2>&1; then
    die "formatter '$formatter' is not installed"
fi
[[ -r "$repo_root/.clang-format" ]] ||
    die "repository configuration '$repo_root/.clang-format' is missing or unreadable"

case "${CLANG_FORMAT_FULL_TREE:-0}" in
    1|true|TRUE|yes|YES)
        full_tree=1
        ;;
    0|false|FALSE|no|NO|'')
        full_tree=0
        ;;
    *)
        die 'CLANG_FORMAT_FULL_TREE must be 0 or 1'
        ;;
esac

base_sha="${BASE_SHA:-}"
head_sha="${HEAD_SHA:-}"
event="${GITHUB_EVENT_NAME:-}"

if [[ "$full_tree" -eq 0 ]]; then
    case "$event" in
        pull_request|push)
            if [[ ! "$base_sha" =~ ^[[:xdigit:]]{40,64}$ ||
                ! "$head_sha" =~ ^[[:xdigit:]]{40,64}$ ]]; then
                full_tree=1
            elif ! git cat-file -e "$base_sha^{commit}" ||
                ! git cat-file -e "$head_sha^{commit}"; then
                full_tree=1
            fi
            ;;
        *)
            full_tree=1
            ;;
    esac
fi

file_list="$(mktemp)"
trap 'rm -f "$file_list"' EXIT

if [[ "$full_tree" -eq 1 ]]; then
    git ls-files -z --cached --others --exclude-standard >"$file_list" ||
        die 'could not enumerate repository files'
else
    git diff --name-only --diff-filter=ACMR -z "$base_sha" "$head_sha" -- >"$file_list" ||
        die "could not enumerate changed files between $base_sha and $head_sha"
fi

files=()
while IFS= read -r -d '' path; do
    case "$path" in
        *.cpp|*.c|*.h|*.hpp)
            [[ -f "$path" ]] && files+=("$path")
            ;;
    esac
done <"$file_list"

if [[ "${#files[@]}" -eq 0 ]]; then
    printf 'clang-format check: no existing .c, .cpp, .h, or .hpp files to check\n'
    exit 0
fi

printf 'clang-format check: checking %d file(s)%s\n' \
    "${#files[@]}" "$([[ "$full_tree" -eq 1 ]] && printf ' (full tree)' || true)"

if ! "$formatter" \
    --style="file:$repo_root/.clang-format" \
    --dry-run \
    --Werror \
    -- "${files[@]}"; then
    printf '%s\n' \
        'clang-format check: formatting violations found.' \
        'Run the pinned clang-format version with the repository .clang-format file, then commit the result.' >&2
    exit 1
fi

printf 'clang-format check: passed\n'
