#!/bin/sh
set -eu

dd if="$1" of="$2" bs=1 count=16 status=none
