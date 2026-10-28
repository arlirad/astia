#!/bin/sh
set -eu

source=$1
test -d "$source/Src"
python3 "$(dirname "$0")/patch-zsh-signals.py" "$source/Src/signals.c"
python3 - "$source/Src/exec.c" <<'PY'
import sys

path = sys.argv[1]
text = open(path).read()
if "ASTIA native fork/process-group sequencing" in text:
    raise SystemExit(0)

old = """    if (pid) {
\tclose(synch[1]);
\tread_loop(synch[0], (char *)&esret, sizeof(esret));
\tclose(synch[0]);"""
new = """    if (pid) {
#ifdef ASTIA /* ASTIA native fork/process-group sequencing */
\t/* Astia's fork syscall publishes the child relationship directly.  The
\t * traditional zsh pipe handshake is only needed to transfer host-side
\t * process-group bookkeeping; waiting for it here prevents a builtin
\t * pipeline from advancing to its RHS. */
\tclose(synch[1]);
\tesret.gleader = pid;
\tesret.list_pipe_job = -1;
#else
\tclose(synch[1]);
\tread_loop(synch[0], (char *)&esret, sizeof(esret));
#endif
\tclose(synch[0]);"""
if old not in text:
    raise SystemExit("zsh execcmd_fork parent handshake not found")
text = text.replace(old, new, 1)

old = """    entersubsh(flags, &esret);
    if (write_loop(synch[1], (const void *) &esret, sizeof(esret)) != sizeof(esret)) {
\tzerr(\"Failed to send entersubsh_ret report: %e\", errno);
\treturn -1;
    }
    close(synch[1]);"""
new = """    entersubsh(flags, &esret);
#ifndef ASTIA /* ASTIA native fork/process-group sequencing */
    if (write_loop(synch[1], (const void *) &esret, sizeof(esret)) != sizeof(esret)) {
\tzerr(\"Failed to send entersubsh_ret report: %e\", errno);
\treturn -1;
    }
#endif
    close(synch[1]);"""
if old not in text:
    raise SystemExit("zsh execcmd_fork child handshake not found")
text = text.replace(old, new, 1)

open(path, "w").write(text)
PY
python3 - "$source/Src/exec.c" <<'PY'
import sys

path = sys.argv[1]
text = open(path).read()
if "ASTIA /dev/null fd-zero fix" not in text:
    old = '\t\tif (open("/dev/null", O_RDWR | O_NOCTTY)) {'
    new = '''#ifdef ASTIA /* ASTIA /dev/null fd-zero fix */
\t\tif (open("/dev/null", O_RDWR | O_NOCTTY) < 0) {
#else
\t\tif (open("/dev/null", O_RDWR | O_NOCTTY)) {
#endif'''
    if old not in text:
        raise SystemExit("zsh async /dev/null open check not found")
    text = text.replace(old, new, 1)
open(path, "w").write(text)
PY
python3 - "$source/Src/exec.c" <<'PY'
import sys

path = sys.argv[1]
text = open(path).read()
if "ASTIA synchronous builtin pipeline stage" in text:
    raise SystemExit(0)
old = """    if ((how & Z_ASYNC) || output ||
\t(last1 == 2 && input && EMULATION(EMULATE_SH))) {"""
new = """    if ((how & Z_ASYNC)
#ifndef ASTIA /* ASTIA synchronous builtin pipeline stage */
        || output
#endif
        || (last1 == 2 && input && EMULATION(EMULATE_SH))) {"""
if old not in text:
    raise SystemExit("zsh early pipeline fork condition not found")
text = text.replace(old, new, 1)
old = """\tif (!do_exec &&
\t    (((is_builtin || is_shfunc) && output) ||
\t     (!is_cursh && (last1 != 1 || nsigtrapped || havefiles() ||
\t\t\t    fdtable_flocks))) {"""
new = """\tif (!do_exec &&
#ifndef ASTIA /* ASTIA synchronous builtin pipeline stage */
\t    (((is_builtin || is_shfunc) && output) ||
#endif
\t     (!is_cursh && (last1 != 1 || nsigtrapped || havefiles() ||
\t\t\t    fdtable_flocks)))) {"""
if old not in text:
    raise SystemExit("zsh builtin pipeline fork condition not found")
text = text.replace(old, new, 1)
open(path, "w").write(text)
PY
python3 - "$source/Src/exec.c" <<'PY'
import sys

path = sys.argv[1]
text = open(path).read()
old = """#ifndef ASTIA /* ASTIA child owns no inherited zsh jobs */
\tif (thisjob != -1)
\t    waitjobs();
#endif
\t_realexit();"""
if old in text:
    pass
else:
    old = """\tif (thisjob != -1)
\t    waitjobs();
\t_realexit();"""
    new = """#ifndef ASTIA /* ASTIA child owns no inherited zsh jobs */
\tif (thisjob != -1)
\t    waitjobs();
#endif
\t_realexit();"""
    if old not in text:
        raise SystemExit("zsh forked-child cleanup wait not found")
    text = text.replace(old, new, 1)
open(path, "w").write(text)
PY
