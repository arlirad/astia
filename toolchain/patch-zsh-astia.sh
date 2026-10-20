#!/bin/sh
set -eu

# Astia uses native process groups, tty ownership, signal delivery, and pipe
# handshakes.  Keep this hook for build compatibility; it must not rewrite zsh.
test -d "$1/Src"
