#!/bin/sh
set -eu

# Astia provides process groups, tty ownership, signal delivery, and pipe
# handshakes natively.  Retain this validation hook for build-script
# compatibility; it must not rewrite zsh or bypass any of those paths.
test -d "$1/Src"
