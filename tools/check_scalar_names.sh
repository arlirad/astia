#!/bin/sh
# Enforce the compact machine-sized scalar vocabulary from common.hpp.
#
# Astia spells the machine word types usz/isz.  The older usize/isize names
# are retired; this guard keeps them from creeping back into kernel, module,
# or documentation sources.
set -eu
cd "$(git rev-parse --show-toplevel)"

if matches=$(git grep -nE '\busize\b|\bisize\b' -- kernel modules docs); then
    printf '%s\n' "usize/isize are retired; use usz/isz:" "$matches" >&2
    exit 1
fi

printf 'scalar naming check: passed\n'
