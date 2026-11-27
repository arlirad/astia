#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sysroot=${1:-${ASTIA_SYSROOT:-}}
crt0=${ASTIA_CRT0:-}
missing=0

report_missing() {
    printf '  %s\n' "$1" >&2
    missing=1
}

require_header() {
    if [ -z "$sysroot" ] || [ ! -f "$sysroot/include/$1" ]; then
        report_missing "$1 ($2)"
    fi
}

require_text() {
    if [ -z "$sysroot" ] || [ ! -f "$sysroot/include/$1" ]; then
        report_missing "$3"
        return
    fi
    if ! grep -Eq "$2" "$sysroot/include/$1"; then
        report_missing "$1: $3"
    fi
}

if [ -z "$sysroot" ]; then
    echo "openssh: no Astia sysroot supplied" >&2
    echo "usage: check-prerequisites.sh SYSROOT" >&2
    exit 2
fi

printf 'openssh: checking prerequisites in %s\n' "$sysroot" >&2

# Transport and address-family support.
require_header netinet/in.h "AF_INET transport, #387/#388"
require_header arpa/inet.h "IPv4 address conversion, #388"
require_header netdb.h "name resolution, #388"
require_header net/if.h "interface configuration, #387/#388"
require_text netinet/in.h 'struct[[:space:]]+sockaddr_in' \
    "IPv4 socket addresses, #388"
require_header unistd.h "process and credential APIs, #244/#394"
require_header sys/socket.h "socket descriptors"
require_text sys/socket.h 'SCM_RIGHTS' "SCM_RIGHTS file-descriptor passing, #389"
require_text sys/socket.h '\bsendmsg\b' "sendmsg(2), #389"
require_text sys/socket.h '\brecvmsg\b' "recvmsg(2), #389"
require_text net/if.h 'struct[[:space:]]+ifreq' "interface configuration, #387/#388"
require_text net/if.h '\bSIOCSIFADDR\b' "interface address configuration, #387/#388"
require_text net/if.h '\bSIOCSIFFLAGS\b' "interface flags, #387/#388"
require_text arpa/inet.h '\binet_pton[[:space:]]*\(' \
    "IPv4 address conversion, #388"

# Daemon and privilege-separation runtime.
require_header syslog.h "syslog(3), #391"
require_text syslog.h '\bsyslog[[:space:]]*\(' "syslog(3), #391"
require_text unistd.h '\bchroot[[:space:]]*\(' "chroot(2), #394"
require_text unistd.h '\bsetresuid[[:space:]]*\(' "setresuid() saved-set-uid, #244"
require_text unistd.h '\bsetresgid[[:space:]]*\(' "setresgid() saved-set-gid, #244"

# Files, users, and the remaining libc surface.
require_header pwd.h "account lookup, #319"
require_header grp.h "group lookup, #319"
require_text pwd.h '\bgetpwnam[[:space:]]*\(' "account lookup, #319"
require_text grp.h '\bgetgrnam[[:space:]]*\(' "group lookup, #319"
require_header sys/select.h "select(2), #247/#288"
require_text sys/select.h '\bselect[[:space:]]*\(' "select(2), #247/#288"
require_header sys/random.h "random-byte syscall, #390"
require_text unistd.h '\bgetentropy[[:space:]]*\(' "getentropy(3), #390"
require_text stdlib.h '\barc4random[[:space:]]*\(' "arc4random(3), #390"
require_header sys/un.h "AF_UNIX sockets"
require_header sys/mman.h "anonymous mappings"
require_header sys/stat.h "file metadata and permissions"
require_header termios.h "TTY session setup"
require_header signal.h "signal handling"

for library in "$sysroot/lib/libastia_libc.a" "$sysroot/lib/libc.so"; do
    if [ ! -f "$library" ]; then
        report_missing "$library (Astia libc sysroot)"
    fi
done

if [ -n "$crt0" ] && [ ! -f "$crt0" ]; then
    report_missing "$crt0 (Astia crt0.o)"
fi

if [ "$missing" -ne 0 ]; then
    echo "openssh: prerequisites missing; the port cannot build against this sysroot" >&2
    echo "openssh: see docs/openssh-port.md for the dependency map" >&2
    exit 3
fi

echo "openssh: prerequisites satisfied" >&2
