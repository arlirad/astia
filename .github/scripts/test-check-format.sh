#!/usr/bin/env bash
set -Eeuo pipefail

die() {
    printf 'clang-format check test: error: %s\n' "$*" >&2
    exit 1
}

repo_root="$(git rev-parse --show-toplevel)" ||
    die 'not inside a Git repository'
checker="$repo_root/.github/scripts/check-format.sh"

test_root="$(mktemp -d)"
trap 'rm -rf "$test_root"' EXIT

git -C "$test_root" init --quiet --initial-branch=main
git -C "$test_root" config user.email test@example.invalid
git -C "$test_root" config user.name 'Format Test'
printf 'Based\n' >"$test_root/.clang-format"
printf 'int a() { return 0; }\n' >"$test_root/a.cpp"
printf 'int b() { return 0; }\n' >"$test_root/b.cpp"
git -C "$test_root" add .clang-format a.cpp b.cpp
git -C "$test_root" commit --quiet -m base
base_sha="$(git -C "$test_root" rev-parse HEAD)"
git -C "$test_root" update-ref refs/remotes/origin/main "$base_sha"

formatter="$test_root/formatter"
cat >"$formatter" <<'EOF'
#!/usr/bin/env bash
printf '%s\0' "$@" >"$FORMATTER_ARGS"
EOF
chmod +x "$formatter"

run_check() {
    local event="$1"
    local base="$2"
    local head="$3"
    local args="$test_root/formatter-args"
    rm -f "$args"
    (
        cd "$test_root"
        GITHUB_EVENT_NAME="$event" \
            BASE_SHA="$base" \
            HEAD_SHA="$head" \
            CLANG_FORMAT_BIN="$formatter" \
            CLANG_FORMAT_FULL_TREE=0 \
            FORMATTER_ARGS="$args" \
            "$checker" >/dev/null
    ) || die "$event check failed"
    printf '%s\n' "$args"
}

assert_files() {
    local args="$1"
    shift
    if ! python3 - "$args" "$@" <<'PY'
import sys

values = [value.decode() for value in open(sys.argv[1], "rb").read().split(b"\0") if value]
expected = set(sys.argv[2:])
actual = {value for value in values if value.endswith((".c", ".cpp", ".h", ".hpp"))}
if actual != expected:
    print("\n".join(sorted(actual)), file=sys.stderr)
    raise SystemExit(1)
PY
    then
        die "unexpected formatter file list in $args"
    fi
}

git -C "$test_root" checkout --quiet -b pull-request "$base_sha"
printf 'int a() { return 1; }\n' >"$test_root/a.cpp"
git -C "$test_root" add a.cpp
git -C "$test_root" commit --quiet -m pr
pr_head="$(git -C "$test_root" rev-parse HEAD)"
pr_args="$(run_check pull_request "$base_sha" "$pr_head")"
assert_files "$pr_args" a.cpp

git -C "$test_root" checkout --quiet -b push "$base_sha"
printf 'int b() { return 1; }\n' >"$test_root/b.cpp"
git -C "$test_root" add b.cpp
git -C "$test_root" commit --quiet -m push
push_head="$(git -C "$test_root" rev-parse HEAD)"
push_args="$(run_check push "$base_sha" "$push_head")"
assert_files "$push_args" b.cpp

git -C "$test_root" checkout --quiet -b rename "$base_sha"
git -C "$test_root" mv a.cpp renamed.cpp
git -C "$test_root" commit --quiet -m rename
rename_head="$(git -C "$test_root" rev-parse HEAD)"
rename_args="$(run_check push "$base_sha" "$rename_head")"
assert_files "$rename_args" renamed.cpp

git -C "$test_root" checkout --quiet -b delete "$base_sha"
git -C "$test_root" rm --quiet a.cpp
git -C "$test_root" commit --quiet -m delete
delete_head="$(git -C "$test_root" rev-parse HEAD)"
delete_args="$(run_check push "$base_sha" "$delete_head")"
[[ ! -e "$delete_args" ]] || die 'deleted file invoked formatter'

git -C "$test_root" checkout --quiet -b initial "$base_sha"
printf 'int a() { return 2; }\n' >"$test_root/a.cpp"
git -C "$test_root" add a.cpp
git -C "$test_root" commit --quiet -m initial
initial_head="$(git -C "$test_root" rev-parse HEAD)"
git -C "$test_root" update-ref refs/remotes/origin/main "$initial_head"
initial_args="$(run_check push "$(printf '0%.0s' {1..40})" "$initial_head")"
assert_files "$initial_args" a.cpp b.cpp

git -C "$test_root" checkout --quiet -b no-main "$base_sha"
git -C "$test_root" update-ref -d refs/remotes/origin/main
git -C "$test_root" update-ref -d refs/heads/main
no_main_head="$(git -C "$test_root" rev-parse HEAD)"
zero_args="$(run_check push "$(printf '0%.0s' {1..40})" "$no_main_head")"
assert_files "$zero_args" a.cpp b.cpp

empty_args="$(run_check push "$base_sha" "$base_sha")"
[[ ! -e "$empty_args" ]] || die 'empty change range invoked formatter'

printf 'clang-format check tests: passed\n'
