# Astia coreutils

Astia coreutils are small native commands built against the Astia libc and
installed in `/bin`. They are not the host GNU coreutils and implement only
the options listed here.

## Common conventions

Commands that accept files use `-` for standard input where stated. Most
commands accept `--help` and `--version`; the command-specific synopsis below
is authoritative. A normal success returns `0`; malformed arguments normally
return `2`, while an I/O or operation failure returns `1`.

## Text and stream commands

| Command | Synopsis | Behavior |
| --- | --- | --- |
| `cat` | `cat [FILE]...` | Copy files to standard output. `-` means standard input. |
| `cut` | `cut -f LIST [-d DELIM] [FILE]...` | Print selected delimiter-separated fields. `LIST` supports individual fields and ranges from 1 through 64. |
| `echo` | `echo [-n] [ARG]...` | Print arguments separated by spaces; `-n` omits the final newline. |
| `grep` | `grep [-i] [-n] [-v] PATTERN [FILE]...` | Match a POSIX basic regular expression. `-i` ignores case, `-n` prefixes line numbers, and `-v` selects non-matching lines. Exit `0` means a match, `1` no match, and `2` an error. |
| `head` | `head [-n NUMBER] [-qv] [FILE]...` | Print the first 10 lines by default. `-n` selects the count, `-q` suppresses file headers, and `-v` always prints them. |
| `sort` | `sort [-r] [-u] [FILE]...` | Sort complete input lines using bytewise C-locale order. `-r` reverses the order and `-u` removes adjacent duplicate lines. |
| `tee` | `tee [-a] [FILE]...` | Copy standard input to standard output and files. `-a` appends instead of truncating. At most 32 output files are accepted. |
| `wc` | `wc [-l] [-w] [-c] [FILE]...` | Print newline, word, and byte counts. With no field option all three are printed; the options select individual counts. |

`grep` supports anchors, `.`, `*`, bracket expressions, intervals,
parenthesized groups, and back-references `\1` through `\9`. `sort` keeps all
input lines in memory before sorting.

## Filesystem commands

| Command | Synopsis | Behavior |
| --- | --- | --- |
| `basename` | `basename NAME [SUFFIX]` | Print the final pathname component and optionally remove `SUFFIX`. |
| `chmod` | `chmod [-R] MODE FILE...` | Change permission bits. `MODE` may be octal or POSIX symbolic mode; `-R` traverses directory trees. |
| `cp` | `cp [-r] SOURCE DEST` | Copy a file or, with `-r`, a directory tree. `-` can name standard input or output. |
| `dirname` | `dirname NAME` | Print the pathname prefix before the final component. |
| `find` | `find [-H] [-L] [-P] [PATH...]` | Print each path and its descendants. `-P` is the default, `-H` follows command-line symlinks, and `-L` follows all symlinks. With no path, `.` is used. |
| `ln` | `ln [-s] [-f] TARGET LINK_NAME` | Create a hard link or, with `-s`, a symbolic link. `-f` replaces an existing link. Long forms `--symbolic` and `--force` are supported. |
| `ls` | `ls [-aAl] [FILE]...` | List directory contents. `-a` includes `.` and `..`, `-A` includes other dot files, and `-l` prints mode, link count, numeric owner IDs, size, timestamp, and symlink targets. |
| `mkdir` | `mkdir [-p] DIRECTORY...` | Create directories. `-p` creates missing parents. |
| `mv` | `mv SOURCE DEST` | Move a file or directory, or copy and remove it when a direct rename is not possible. |
| `readlink` | `readlink PATH` | Print a symbolic link target without following the link. |
| `rm` | `rm [-f] [-r] FILE...` | Remove files and directory trees. `-f` ignores missing paths and `-r`/`-R` enables recursive removal. The root directory is rejected. |
| `touch` | `touch [-acm] FILE...` | Create missing files and update timestamps. `-c` disables creation, `-m` updates modification time, and `-a` requests access time. |

`touch` uses the native timestamp ABI. It rejects unsupported explicit-time
forms and does not fake access-time behavior. `cp`, `mv`, and `rm` protect
against directory recursion and symlink races according to their native
filesystem implementation.

## Process and account commands

| Command | Synopsis | Behavior |
| --- | --- | --- |
| `env` | `env [-s] [NAME=VALUE]... [COMMAND [ARG]...]` | Print the environment or run a command with assignments applied. `-s`/`--sorted` sorts complete `NAME=VALUE` entries without changing the process environment. |
| `id` | `id` | Print effective UID, GID, and supplementary groups with names. |
| `login` | `login` | Read a username from the controlling terminal, establish a session, load `/etc/passwd` and `/etc/group`, set credentials, and exec the account shell. |
| `ps` | `ps` | Print one process snapshot with PID, parent and process-group IDs, UID, GID, state, controlling terminal, and command name. |
| `pwd` | `pwd` | Print the current working directory. |
| `whoami` | `whoami` | Print the effective account name. |

`login` has no PAM, shadow, crypt, network, or password backend. It accepts
only the `x` convention and an explicitly empty password field.

## System commands

| Command | Synopsis | Behavior |
| --- | --- | --- |
| `poweroff` | `poweroff` | Request the Astia power-off transition. |
| `reboot` | `reboot` | Request the Astia reboot transition. |
| `uname` | `uname [-a] [-snrvm]` | Print system name, node name, release, version, and machine. `-a` selects all fields; the other flags select individual fields. |
| `true` | `true` | Exit successfully. |
| `false` | `false` | Exit unsuccessfully. |

`poweroff` and `reboot` require the kernel's power-operation permission and
return an error if the platform cannot perform the transition.

## Service command

```text
daemonctl status
daemonctl start
daemonctl stop
daemonctl restart
daemonctl validate
daemonctl list
daemonctl config validate
daemonctl config list
```

`daemonctl` sends the corresponding request to the Astia daemon supervisor
over its Unix control socket. `validate` and `list` are aliases for
`config validate` and `config list`. The command retries a short transient
connect window, then returns status `1` on connection, protocol, or supervisor
errors and status `2` for an invalid command.

## Build and test

Build the complete set with:

```sh
ASTIA_SYSROOT=<sysroot> ASTIA_CRT0=<crt0.o> utils/coreutils/build.sh
```

The QEMU regression suite is:

```sh
utils/coreutils/tests/run.sh
```
