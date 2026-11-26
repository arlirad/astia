#!/bin/sh
# Enforce the astia::strings rules from docs/strings.md.
#
# The only string types are astia::strings::string and astia::string_view. The
# string_*-prefixed free functions are retired, and freestanding kernel and
# module translation units must not reach for the C++ standard library.
set -eu
cd "$(git rev-parse --show-toplevel)"

status=0

fail() {
    status=1
    printf '%s\n' "$@"
}

if matches=$(git grep -nE '\bstring_\b|\bstring_(starts_with|compare|before_nul)\b' -- \
        kernel modules libc); then
    fail "string_-prefixed vocabulary is retired (docs/strings.md):" "$matches"
fi

if matches=$(git grep -nE '#[[:space:]]*include[[:space:]]*<(string|string_view)>|std::string' -- \
        kernel/include kernel/src modules ':!modules/tests'); then
    fail "kernel and module sources must not include <string>/<string_view> or use std::string:" \
        "$matches"
fi

if matches=$(git grep -nE 'astia::string\b' -- kernel modules libc); then
    fail "the owning string type lives in astia::strings; astia::string is retired:" \
        "$matches"
fi

if [ "${status}" -ne 0 ]; then
    exit 1
fi

printf 'string naming and inclusion check: passed\n'
