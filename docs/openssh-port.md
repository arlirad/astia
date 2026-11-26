# OpenSSH port

This document records the OpenSSH portable source pin, the cross-build and
packaging workflow, the safe test configuration, and the acceptance tests.
The port is intentionally test-only: normal Astia images do not install
`sshd`, host keys, an `sshd` account, or `/etc/ssh`.

## Source pin

The reviewed source is OpenSSH portable `V_9_9_P2`, pinned as the
`user/openssh` submodule at commit
`d76b2675116617394cd355a3437b4963a562b64d` (the release commit carrying the
generated `configure` script).  `toolchain/init-userspace.sh` initializes the
submodule and rejects a checkout that is not at that commit.  The build is
out of tree; no upstream file is edited.

## Prerequisite status

The port consumes the ABI work from the prerequisite issues.  On current
`main` those dependencies are still open, so `toolchain/build-openssh.sh`
stops in its prerequisite gate with status 3 and a named list instead of
starting a configure run that cannot succeed.  The QEMU acceptance tests treat
status 3 as a skip (`ctest` reports `Skipped`), never as a pass.

| Prerequisite | Issue | Gate in `toolchain/openssh/check-prerequisites.sh` |
| --- | --- | --- |
| TCP/IP transport and `AF_INET` sockets | #387 | `net/if.h`, `netinet/in.h`, `SIOCSIFADDR`, `ifreq` |
| INET address and resolver APIs | #388 | `sockaddr_in`, `arpa/inet.h`, `netdb.h`, `inet_pton` |
| `sendmsg` / `recvmsg` and `SCM_RIGHTS` | #389 | `sys/socket.h` declarations and `SCM_RIGHTS` |
| Kernel entropy | #390 | `getentropy` in `sys/random.h` (landed) |
| `syslog` | #391 | `syslog.h` and `syslog()` |
| `chroot(2)` | #394 | `chroot()` in `unistd.h` |
| Saved-set-ID credentials | #244 | `setresuid()`, `setresgid()` |
| Authentication backend | #395 | key-only test image, no password/PAM backend |
| Crypto backend | #396 | `--without-openssl` internal crypto |

The account/group parser fixes and the descriptor, signal, process, and
permission fixes tracked separately remain part of the runtime acceptance
criteria; the port cannot claim them complete.

## Build

Initialize the pinned sources and build:

```sh
toolchain/init-userspace.sh
toolchain/build-openssh.sh
```

The default build directory is `build/openssh`, matching the issue's
validation commands.  Override it with `OPENSSH_BUILD`, the source with
`OPENSSH_SOURCE`, the Astia sysroot with `ASTIA_SYSROOT`, and the startup
object with `ASTIA_CRT0`.

The script builds the Astia sysroot through `toolchain/build-init-libc.sh`
when it is missing, then runs the prerequisite gate.  It produces:

```text
build/openssh/output/ssh
build/openssh/output/sshd
build/openssh/output/sshd-session
build/openssh/output/ssh-keygen
build/openssh/etc/sshd_config
```

OpenSSH 9.9 moved the post-authentication server side into the
`sshd-session` helper, so the port installs it under `/usr/libexec` as part of
the server package.  `scp`, `sftp`, `ssh-agent`, and the PKCS#11/FIDO helpers
are not built in this milestone.

The exact configure invocation is recorded in `toolchain/build-openssh.sh`:

```text
--host=x86_64-unknown-elf --build=x86_64-pc-linux-gnu
--prefix=/usr --bindir=/usr/bin --sbindir=/usr/sbin
--libexecdir=/usr/libexec --sysconfdir=/etc/ssh
--with-privsep-user=sshd --with-privsep-path=/var/empty
--without-openssl --without-zlib --without-pam --without-shadow
--without-selinux --without-audit --without-libedit --without-kerberos5
--without-rpath
--disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx
--disable-lastlog --disable-libutil --disable-pututline --disable-pututxline
--disable-pkcs11 --disable-security-key --disable-etc-default-login
--disable-strip
```

Configure answers that would otherwise run host programs are supplied through
`config.site`; compilation uses the Astia cross libc headers, `crt0.o`, and
compiler builtins.  The binaries are dynamically linked against `libc.so` and
`/lib/ld.astia.so`, following the existing zsh and nano sysroot policy.

`--without-pam` is kept in the command line because the issue records it, but
OpenSSH 9.9 has no such configure option; configure prints an unrecognized
option warning and PAM remains off by default.

## Test image layout

`toolchain/package-openssh-initrd.sh BASE_INITRD OPENSSH_BUILD OUTPUT_INITRD`
adds the test-only layout:

```text
/usr/bin/ssh
/usr/bin/ssh-keygen
/usr/sbin/sshd
/usr/libexec/sshd-session
/etc/ssh/sshd_config
/etc/ssh/ssh_host_ed25519_key{,.pub}
/etc/ssh/test_client_key{,.pub}
/etc/ssh/authorized_keys
/etc/passwd          (user plus locked sshd privsep account)
/etc/group           (users plus sshd privsep group)
/var/empty           root-owned, mode 0755
/run
```

The packaging script generates throwaway Ed25519 host and client keys in the
ignored build tree (`build/openssh/test` by default), or copies keys supplied
through `OPENSSH_HOST_KEY`, `OPENSSH_CLIENT_KEY`, and
`OPENSSH_AUTHORIZED_KEYS`.  Generated private keys are never tracked in the
source tree and must not be reused outside tests.  `OPENSSH_TEST_INIT` installs
the dedicated test init as `/sbin/init`, and `OPENSSH_SHELL_BINARY` supplies the
guest shell needed to execute commands over SSH.

The `sshd` account is locked (`!` password field), has uid/gid 74, and uses
`/var/empty` as its home directory.  The test image's `user` account (uid
1000) is the only account allowed to log in.

## Safe configuration

`toolchain/openssh/sshd_config` is the source of truth installed by the
packaging script.  It enables key-only authentication and sets:

```text
AddressFamily inet
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
AuthenticationMethods publickey
PermitRootLogin no
UsePAM no
UseDNS no
StrictModes yes
X11Forwarding no
AllowAgentForwarding no
AllowTcpForwarding no
AllowUsers user
```

`UsePAM` is logged as an unsupported option because the build has no PAM; the
resulting behaviour is PAM disabled.  The normal Astia development images never
install this file or start `sshd`.

## QEMU acceptance tests

Both tests use the RTL8139 device, QEMU user-mode networking, and a
host-forwarded loopback port:

```sh
ctest --test-dir build-tests -R '^openssh_sshd_qemu$' --output-on-failure
ctest --test-dir build-tests -R '^openssh_ssh_loopback_qemu$' --output-on-failure
```

The tests require external test opt-in.  They build and package the port, boot
a dedicated init that configures `eth0` for QEMU user networking
(`10.0.2.15/24`), runs `sshd -t`, and starts `sshd -D`.  The host then uses the
host's `ssh` binary with the generated test key.  The server test proves:

- a public-key command executes and the session exits cleanly;
- an unauthorized key is rejected;
- password authentication is rejected;
- root login is rejected.

The loopback test additionally runs the guest `/usr/bin/ssh` against
`127.0.0.1` and verifies the host key, key handling, a command result, and a
clean exit.  A guest shell is required for command execution; set
`OPENSSH_SHELL_BINARY` (or `ASTIA_OPENSSH_SHELL` for CTest) to a built Astia
zsh.  Without a shell the test skips instead of claiming coverage.

When the prerequisite gate reports missing APIs the tests exit 77 with the
missing list.  A booted image that fails to configure the interface, fails
`sshd -t`, or never accepts the test key exits non-zero with the serial log;
the runner has a bounded boot timeout and never waits indefinitely.

## Known limitations

- Key-only Ed25519 authentication; password, shadow, and PAM are out of scope
  until #395.
- Internal crypto only; OpenSSL, zlib, FIDO, and PKCS#11 are disabled until
  #396 selects a backend.
- IPv4 only.  IPv6 and DNS resolution follow #388.
- No `scp`, `sftp`, `ssh-agent`, agent forwarding, port forwarding, or X11
  forwarding in this milestone.
- The test configuration and host key are throwaway artifacts.  Production
  host-key provisioning and deployment hardening are follow-ups.
- The `sshd` privilege-separation path still depends on the open `chroot`,
  `SCM_RIGHTS`, saved-set-ID, and syslog issues; the port does not waive them.
