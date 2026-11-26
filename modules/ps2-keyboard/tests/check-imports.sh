#!/usr/bin/env sh
set -eu

object=${1:?usage: check-imports.sh <module.o>}
if command -v llvm-nm >/dev/null 2>&1; then
    nm=llvm-nm
else
    nm=nm
fi

symbols=$("$nm" -u "$object" | awk 'NF { print $NF }' | LC_ALL=C sort -u)

allowed='_ZN5astia3log6detail4emitENS0_5levelEPKcj
_ZN5astia3log6detail6numberENS0_5levelEyjb
_ZN5astia3log6detail6prefixERNS1_14record_builderENS0_5levelEPKcj
_ZN5astia6module13ps2_claim_irqEPFvvERNS0_14lifetime_tokenE
_ZN5astia6module14lifetime_token7acquireEv
_ZN5astia6module14lifetime_token7releaseEv
_ZN5astia6module14lifetime_tokenD1Ev
_ZN5astia6module14lifetime_tokenaSEOS1_
_ZN5astia6module14ps2_unmask_irqEv
_ZN5astia6module15tty_input_writeEh
_ZN5astia6module18ps2_platform_portsEv
_ZNK5astia6module14lifetime_token13make_residentEv
_ZNK5astia6module14lifetime_token5validEv
memcpy
memset
memmove'

fail=0
for symbol in $symbols; do
    if ! printf '%s\n' "$allowed" | grep -qxF "$symbol"; then
        printf 'unexpected module import: %s\n' "$symbol" >&2
        fail=1
    fi
done

for required in \
    _ZN5astia3log6detail4emitENS0_5levelEPKcj \
    _ZN5astia3log6detail6prefixERNS1_14record_builderENS0_5levelEPKcj \
    _ZN5astia6module13ps2_claim_irqEPFvvERNS0_14lifetime_tokenE \
    _ZN5astia6module14ps2_unmask_irqEv \
    _ZN5astia6module15tty_input_writeEh \
    _ZN5astia6module18ps2_platform_portsEv \
    _ZNK5astia6module14lifetime_token13make_residentEv; do
    if ! printf '%s\n' "$symbols" | grep -qxF "$required"; then
        printf 'missing required module import: %s\n' "$required" >&2
        fail=1
    fi
done

if printf '%s\n' "$symbols" | grep -Eq '(_ZN5astia4arch|_ZN5astia3dev.*(pio|uart|virtual_tty)|inb|outb|device_pio)'; then
    printf 'kernel-private platform symbol imported\n' >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    printf 'module imports:\n%s\n' "$symbols" >&2
    exit 1
fi

printf 'ps2-keyboard imports are limited to the normal module platform and sink API\n'
