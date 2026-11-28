#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${COREUTILS_TEST_BUILD:-$root/utils/coreutils/tests/build}
sysroot=${ASTIA_SYSROOT:-/tmp/astia-zsh-build/sysroot}
crt0=${ASTIA_CRT0:-/tmp/astia-zsh-build/astia/image/crt0.o}
kernel=${ASTIA_KERNEL:-$root/kernel/build-tests/src/astiakrn_runtime}
kernel_build=$(CDPATH= cd -- "$(dirname -- "$kernel")/.." && pwd)
clang=${CLANG:-clang}
qemu=${QEMU:-qemu-system-x86_64}
cpio=${ASTIA_CPIO:-cpio}
target=x86_64-unknown-elf
qemu_timeout_seconds=600
expected_qemu_status=1
resource=$($clang --target=$target -print-resource-dir)

command -v "$qemu" >/dev/null 2>&1 || {
    echo "QEMU executable not found: $qemu" >&2
    exit 1
}
command -v timeout >/dev/null 2>&1 || {
    echo "timeout command not found (required for the regression test)" >&2
    exit 1
}
command -v "$cpio" >/dev/null 2>&1 || {
    echo "cpio executable not found: $cpio" >&2
    exit 1
}

rm -rf "$out"
mkdir -p "$out/bin" "$out/root/bin" "$out/root/sbin"
ASTIA_SYSROOT="$sysroot" ASTIA_CRT0="$crt0" COREUTILS_BUILD="$out" \
    "$root/utils/coreutils/build.sh"
mkdir -p "$out/host"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/ls_layout_test.c" -o "$out/host/ls_layout_test"
"$out/host/ls_layout_test"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/grep_regex_test.c" -o "$out/host/grep_regex_test"
"$out/host/grep_regex_test"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/daemonctl_path_test.c" -o "$out/host/daemonctl_path_test"
"$out/host/daemonctl_path_test"
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -Dmain=cu_touch_main -Dutimensat=cu_test_utimensat \
    -I "$root/utils/coreutils/src" -c "$root/utils/coreutils/src/touch.c" \
    -o "$out/host/touch_times_touch.o"
$clang -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/tests/touch_times_test.c" \
    "$out/host/touch_times_touch.o" -o "$out/host/touch_times_test"
"$out/host/touch_times_test"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/sort_growth_test.c" -o "$out/host/sort_growth_test"
"$out/host/sort_growth_test"
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/src/sort.c" -o "$out/host/sort_sanitize"
printf 'b\na\nb\n' | "$out/host/sort_sanitize" -u >"$out/host/sort_unique_interleaved.out"
printf 'a\nb\n' >"$out/host/sort_unique_interleaved.expected"
cmp "$out/host/sort_unique_interleaved.expected" "$out/host/sort_unique_interleaved.out"
printf 'delta\nalpha\ncharlie\nbravo\nalpha\ndelta\n' | \
    "$out/host/sort_sanitize" -u >"$out/host/sort_unique_scattered.out"
printf 'alpha\nbravo\ncharlie\ndelta\n' >"$out/host/sort_unique_scattered.expected"
cmp "$out/host/sort_unique_scattered.expected" "$out/host/sort_unique_scattered.out"
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function -I "$root/utils/coreutils/src" \
    -Dmain=cu_tee_main -c "$root/utils/coreutils/src/tee.c" -o "$out/host/tee_eintr_tee.o"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/tee_eintr_test.c" "$out/host/tee_eintr_tee.o" \
    -o "$out/host/tee_eintr_test"
"$out/host/tee_eintr_test"
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -I "$root/utils/coreutils/src" -Dmain=head_main \
    -Dwrite=head_test_write -c "$root/utils/coreutils/src/head.c" \
    -o "$out/host/head_write_head.o"
$clang -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/tests/head_write_test.c" \
    "$out/host/head_write_head.o" -o "$out/host/head_write_test"
"$out/host/head_write_test"

# wc: only the exact operand "-" selects standard input; a one-character
# filename is opened as a file.
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/src/wc.c" -o "$out/host/wc"
mkdir -p "$out/host/wc_one" "$out/host/wc_missing"
printf 'one\n' >"$out/host/wc_one/a"
(cd "$out/host/wc_one" && "$out/host/wc" a) >"$out/host/wc_one.out"
printf '1 1 4 a\n' >"$out/host/wc_one.expected"
cmp "$out/host/wc_one.expected" "$out/host/wc_one.out"
if (cd "$out/host/wc_missing" && printf 'stdin\n' | "$out/host/wc" a) \
    >"$out/host/wc_missing.out" 2>"$out/host/wc_missing.err"; then
    echo "wc accepted a missing one-character file" >&2
    exit 1
fi
grep -qF 'wc: a' "$out/host/wc_missing.err"
test ! -s "$out/host/wc_missing.out"
printf 'dash\n' | "$out/host/wc" - >"$out/host/wc_dash.out"
printf '1 1 5 -\n' >"$out/host/wc_dash.expected"
cmp "$out/host/wc_dash.expected" "$out/host/wc_dash.out"
printf 'bare\n' | "$out/host/wc" >"$out/host/wc_bare.out"
printf '1 1 5\n' >"$out/host/wc_bare.expected"
cmp "$out/host/wc_bare.expected" "$out/host/wc_bare.out"

# close() failure injection: link each tool with its main renamed and fail a
# chosen close() call through the linker's --wrap hook.
close_host_flags="-std=c11 -Wall -Wextra -Werror -Wno-unused-function \
-D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -include stdio.h -I $root/utils/coreutils/src"
for tool in head wc grep sort cp mv; do
    # shellcheck disable=SC2086
    $clang $close_host_flags -Dmain=cu_${tool}_main \
        -c "$root/utils/coreutils/src/$tool.c" -o "$out/host/${tool}_close.o"
done
# shellcheck disable=SC2086
$clang $close_host_flags "$root/utils/coreutils/tests/close_error_test.c" \
    "$out/host/head_close.o" "$out/host/wc_close.o" "$out/host/grep_close.o" \
    "$out/host/sort_close.o" "$out/host/cp_close.o" "$out/host/mv_close.o" \
    -Wl,--wrap=close -o "$out/host/close_error_test"
"$out/host/close_error_test"
$clang -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/mv_symlink_test.c" "$out/host/mv_close.o" \
    -o "$out/host/mv_symlink_test"
"$out/host/mv_symlink_test"
$clang -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/mv_dest_symlink_test.c" "$out/host/mv_close.o" \
    -o "$out/host/mv_dest_symlink_test"
"$out/host/mv_dest_symlink_test"
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
    -D_DEFAULT_SOURCE -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/login_read_test.c" -o "$out/host/login_read_test"
"$out/host/login_read_test"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/read_retry_test.c" -o "$out/host/read_retry_test" \
    -Wl,--wrap=read
"$out/host/read_retry_test"
$clang -std=c11 -Wall -Wextra -Werror -I "$root/utils/coreutils/src" -I "$root/kernel/include" \
    "$root/utils/coreutils/tests/ps_state_test.c" -o "$out/host/ps_state_test"
"$out/host/ps_state_test"
$clang -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-unused-function -pthread \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/tests/tree_walk_test.c" \
    -o "$out/host/tree_walk_test"
"$out/host/tree_walk_test"
$clang -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/tests/chmod_mode_test.c" \
    -o "$out/host/chmod_mode_test"
"$out/host/chmod_mode_test"
$clang -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/tests/rm_root_guard_test.c" \
    -o "$out/host/rm_root_guard_test"
"$out/host/rm_root_guard_test"
$clang -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/tests/rm_symlink_swap_test.c" \
    -o "$out/host/rm_symlink_swap_test"
"$out/host/rm_symlink_swap_test"
$clang -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/src/rm.c" -o "$out/host/rm"
mkdir -p "$out/host/rm_guard"
ln -s / "$out/host/rm_guard/rootlink"
if "$out/host/rm" -r "$out/host/rm_guard/rootlink" 2>"$out/host/rm_guard.err"; then
    echo "rm accepted a symlink resolving to /" >&2
    exit 1
fi
test -L "$out/host/rm_guard/rootlink"
grep -qF 'refusing to remove the filesystem root' "$out/host/rm_guard.err"
mkdir -p "$out/host/rm_guard/tree/nested"
printf 'leaf\n' >"$out/host/rm_guard/tree/nested/file"
"$out/host/rm" -r "$out/host/rm_guard/tree"
test ! -e "$out/host/rm_guard/tree"
rm -f "$out/host/rm_guard/rootlink"
rmdir "$out/host/rm_guard"
mkdir -p "$out/root/bin" "$out/root/sbin" "$out/root/etc"

# A source operand that starts with '-' is a file when such a file exists.
$clang -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -I "$root/utils/coreutils/src" "$root/utils/coreutils/src/cp.c" -o "$out/host/cp"
cp_dash_root=$(mktemp -d)
mkdir -p "$cp_dash_root/dir"
printf 'dash file\n' >"$cp_dash_root/-file"
( cd "$cp_dash_root" && "$out/host/cp" -file dir )
test -f "$cp_dash_root/dir/-file"
grep -qxF 'dash file' "$cp_dash_root/dir/-file"
rm -rf "$cp_dash_root"

# cp -r copies symlinks found inside the tree as symlinks instead of following
# them, so a link back to an ancestor terminates (issue #799).
cp_link_root="$out/host/cp_links"
mkdir -p "$cp_link_root/tree/sub"
printf 'leaf\n' >"$cp_link_root/tree/file"
ln -s file "$cp_link_root/tree/link"
ln -s .. "$cp_link_root/tree/sub/up"
timeout 60 "$out/host/cp" -r "$cp_link_root/tree" "$cp_link_root/copy"
test -L "$cp_link_root/copy/link"
test "$(readlink "$cp_link_root/copy/link")" = "file"
test -L "$cp_link_root/copy/sub/up"
test "$(readlink "$cp_link_root/copy/sub/up")" = ".."
test -f "$cp_link_root/copy/file"
grep -qxF 'leaf' "$cp_link_root/copy/file"

# A symlink named on the command line keeps its non-recursive cp semantics:
# the target is copied, not the link.
printf 'target\n' >"$cp_link_root/source"
ln -s source "$cp_link_root/source_link"
"$out/host/cp" "$cp_link_root/source_link" "$cp_link_root/source_copy"
test -f "$cp_link_root/source_copy"
test ! -L "$cp_link_root/source_copy"
grep -qxF 'target' "$cp_link_root/source_copy"

# ln TARGET DIR creates DIR/<basename TARGET>, including the trailing-slash
# form, and keeps relative symbolic-link targets verbatim (issue #819).
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -Dmain=cu_ln_main -I "$root/utils/coreutils/src" \
    -c "$root/utils/coreutils/src/ln.c" -o "$out/host/ln_dir_ln.o"
$clang -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
    -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/tests/ln_dir_test.c" "$out/host/ln_dir_ln.o" \
    -o "$out/host/ln_dir_test"
"$out/host/ln_dir_test"

# mkdir -p must accept any path the host filesystem can represent up to
# PATH_MAX; the QEMU guest cannot receive such arguments yet (issue #184
# limits exec argv and AIO paths to 256 bytes).
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/src/mkdir.c" -o "$out/host/mkdir"
repeat_character() {
    repeat_value=
    repeat_index=0
    while [ "$repeat_index" -lt "$2" ]; do
        repeat_value=$repeat_value$1
        repeat_index=$((repeat_index + 1))
    done
    printf '%s' "$repeat_value"
}
long_path_root=$(mktemp -d)
trap 'rm -rf "$long_path_root"' EXIT
long_component_a=$(repeat_character a 100)
long_component_b=$(repeat_character b 100)
long_component_c=$(repeat_character c 100)
long_path="$long_path_root/$long_component_a/$long_component_b/$long_component_c/leaf"
test "${#long_path}" -gt 255
"$out/host/mkdir" -p "$long_path"
test -d "$long_path"
"$out/host/mkdir" -p "$long_path"
test -d "$long_path"
overlong_path="$long_path_root/$(repeat_character x 5000)"
if "$out/host/mkdir" -p "$overlong_path" 2>"$out/host/mkdir_too_long.err"; then
    echo "mkdir -p accepted a path longer than PATH_MAX" >&2
    exit 1
fi
grep -qF 'File name too long' "$out/host/mkdir_too_long.err"

# ls -l must print complete symlink targets; the old 256-byte buffer cut
# targets longer than 255 bytes short.
$clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -D_POSIX_C_SOURCE=200809L -I "$root/utils/coreutils/src" \
    "$root/utils/coreutils/src/ls.c" -o "$out/host/ls"
for target_length in 255 256 300 4095; do
    long_target=$(repeat_character l "$target_length")
    long_link="$long_path_root/link-$target_length"
    ln -s "$long_target" "$long_link"
    "$out/host/ls" -l "$long_link" >"$out/host/ls_long_target_$target_length.out"
    grep -qF " -> $long_target" "$out/host/ls_long_target_$target_length.out"
done

# ls hides dot entries by default; -a adds . and .., -A adds dotfiles only.
hidden_dir="$out/host/ls_hidden"
mkdir -p "$hidden_dir"
: >"$hidden_dir/visible"
: >"$hidden_dir/.hidden"
printf 'visible\n' >"$out/host/ls_hidden_default.expected"
"$out/host/ls" "$hidden_dir" >"$out/host/ls_hidden_default.out"
cmp "$out/host/ls_hidden_default.expected" "$out/host/ls_hidden_default.out"
printf '.\n..\n.hidden\nvisible\n' >"$out/host/ls_hidden_all.expected"
"$out/host/ls" -a "$hidden_dir" >"$out/host/ls_hidden_all.out"
cmp "$out/host/ls_hidden_all.expected" "$out/host/ls_hidden_all.out"
printf '.hidden\nvisible\n' >"$out/host/ls_hidden_almost.expected"
"$out/host/ls" -A "$hidden_dir" >"$out/host/ls_hidden_almost.out"
cmp "$out/host/ls_hidden_almost.expected" "$out/host/ls_hidden_almost.out"
"$out/host/ls" -la "$hidden_dir" >"$out/host/ls_hidden_long_all.out"
grep -q ' \.$' "$out/host/ls_hidden_long_all.out"
grep -q ' \.\.$' "$out/host/ls_hidden_long_all.out"
grep -q ' \.hidden$' "$out/host/ls_hidden_long_all.out"
grep -q ' visible$' "$out/host/ls_hidden_long_all.out"

common="--target=$target -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone"
# shellcheck disable=SC2086
$clang $common -fno-builtin -nostdinc -isystem "$resource/include" \
    -isystem "$sysroot/include" -c "$root/utils/coreutils/tests/coreutils_test.c" \
    -o "$out/coreutils_test.o"
# shellcheck disable=SC2086
$clang $common -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie \
    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -o "$out/root/sbin/init" \
    "$crt0" "$out/coreutils_test.o" "$sysroot/lib/libastia_libc.a" \
    "$resource/lib/linux/libclang_rt.builtins-x86_64.a"
cp "$out/bin/"* "$out/root/bin/"
for key in "$out/.keys/"*; do
    [ -f "$key" ] || continue
    [ "$(basename "$key")" = sources ] && continue
    cp "$key" "$out/root/bin/$(basename "$key").key"
done
cp "$root/daemond/passwd" "$out/root/etc/passwd"
cp "$root/daemond/group" "$out/root/etc/group"
init_libc=$kernel_build/tests/fixtures/init_libc
init_libc=${ASTIA_INIT_LIBC:-$init_libc}
test -f "$init_libc" || {
    echo "Astia init_libc fixture missing: $init_libc" >&2
    exit 1
}
cp "$init_libc" "$out/root/bin/"
# The libc-backed fixtures are dynamically linked: stage the interpreter and
# the shared libc next to them.
ld_so=${ASTIA_LD_SO:-$kernel_build/tests/fixtures/ld.astia.so}
for required in "$sysroot/lib/libc.so" "$ld_so"; do
    test -f "$required" || {
        echo "Astia dynamic runtime missing: $required" >&2
        exit 1
    }
done
mkdir -p "$out/root/lib"
cp "$sysroot/lib/libc.so" "$out/root/lib/libc.so"
cp "$ld_so" "$out/root/lib/ld.astia.so"
(cd "$out/root" && find sbin bin etc lib -print |
    "$cpio" -o --format=newc --quiet) >"$out/initrd.cpio"

log="$out/qemu.log"
set +e
timeout "$qemu_timeout_seconds" "$qemu" -m 128M -kernel "$kernel" -initrd "$out/initrd.cpio" \
    -serial "file:$log" -display none -monitor none -device isa-debug-exit -no-reboot
status=$?
set -e
cat "$log"
test "$status" -eq "$expected_qemu_status"
grep -qF 'coreutils tests ok' "$log"
