#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=${ASTIA_ZSH_ZSHENV_CHECK_BUILD:-$root/build/zshenv-check}

fail() {
    echo "zshenv prompt check: $*" >&2
    exit 1
}

extract_heredocs() {
    launcher=$1
    prefix=$2
    awk -v prefix="$prefix" '
        /etc\/zshenv/ && /<<.EOF./ { inside = 1; count++; next }
        inside && /^EOF$/ { inside = 0; next }
        inside { print > (prefix "-" count ".zshenv") }
    ' "$launcher"
}

check_prompt_first() {
    file=$1
    first=$(awk 'NF && $1 !~ /^#/ { print; exit }' "$file")
    case $first in
        PS1=*) ;;
        *) fail "$file does not assign PS1 before optional setup: ${first:-<empty>}" ;;
    esac
}

prompt_of() {
    file=$1
    stderr=$file.stderr
    prompt=$(zsh -f -d -c 'source "$1"; print -r -- "$PS1"' zsh "$file" \
        2>"$stderr") || fail "$file could not be sourced by zsh"
    if [ -s "$stderr" ]; then
        cat "$stderr" >&2
        fail "$file produced stderr while sourcing"
    fi
    printf '%s\n' "$prompt"
}

check_launcher() {
    launcher=$1
    prefix=$2
    extract_heredocs "$launcher" "$prefix"
    count=0
    serial=0
    vga=0
    for file in "$prefix"-*.zshenv; do
        [ -e "$file" ] || fail "$launcher does not generate /etc/zshenv"
        count=$((count + 1))
        check_prompt_first "$file"
        if command -v zsh >/dev/null 2>&1; then
            prompt=$(prompt_of "$file")
            case $prompt in
                *ASTIA_VGA_READY*) vga=$((vga + 1)) ;;
                *ASTIA_READY*) serial=$((serial + 1)) ;;
                *) fail "$file sets an unexpected prompt: $prompt" ;;
            esac
        fi
    done
    [ "$count" -gt 0 ] || fail "$launcher does not generate /etc/zshenv"
    if command -v zsh >/dev/null 2>&1; then
        printf '%s %s %s\n' "$launcher" "$serial" "$vga"
    fi
}

case $work in
    ''|/) fail "ASTIA_ZSH_ZSHENV_CHECK_BUILD must identify a working directory" ;;
esac
rm -rf "$work"
mkdir -p "$work"

serial_result=$(check_launcher "$root/toolchain/run-zsh-qemu.sh" "$work/serial")
vga_result=$(check_launcher "$root/toolchain/run-zsh-vga-qemu.sh" "$work/vga")

if command -v zsh >/dev/null 2>&1; then
    set -- $serial_result
    [ "$2" -ge 1 ] || fail "serial launcher does not set ASTIA_READY"
    set -- $vga_result
    [ "$2" -ge 1 ] || fail "VGA serial fallback does not set ASTIA_READY"
    [ "$3" -ge 1 ] || fail "VGA launcher does not set ASTIA_VGA_READY"
fi
