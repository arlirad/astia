#!/bin/sh
set -eu

source=$1

python3 - "$source/Src/jobs.c" "$source/Src/exec.c" \
    "$source/Src/utils.c" "$source/Src/init.c" "$source/Src/signals.c" <<'PY'
import re
import sys

jobs_path, exec_path, utils_path, init_path, signals_path = sys.argv[1:]


def read(path):
    with open(path) as stream:
        return stream.read()


def write(path, value):
    with open(path, "w") as stream:
        stream.write(value)


def remove_block(value, marker):
    pattern = r"\n#ifdef ASTIA /\* " + re.escape(marker) + r".*?\n#endif\n"
    return re.sub(pattern, "\n", value, flags=re.S)


# Foreground execution must remain synchronous.  The old port workaround set
# nowait and skipped zsh's job state machine, which left the shell unable to
# execute the next line after the child exited.
exec_source = read(exec_path)
exec_source = remove_block(exec_source, "ASTIA foreground")
# Without monitor/process-group support zsh can leave the foreground job
# marked LOCKED, which skips waitjobs() entirely.  Keep the normal synchronous
# execution loop, but clear only this bookkeeping bit for Astia.
lock_block = "#ifdef ASTIA /* ASTIA foreground job is not process-group locked */\n        jn->stat &= ~STAT_LOCKED;\n#endif\n"
if "ASTIA foreground job is not process-group locked" not in exec_source:
    needle = "\t    for (; !nowait;) {"
    if needle not in exec_source:
        needle = "    for (; !nowait;) {"
    if needle not in exec_source:
        raise SystemExit("foreground loop boundary not found")
    exec_source = exec_source.replace(needle, lock_block + needle, 1)
if "ASTIA foreground execution is synchronous" not in exec_source:
    needle = "    errbrk_saved = 0;\n"
    replacement = needle + "#ifdef ASTIA /* ASTIA foreground execution is synchronous */\n    nowait = 0;\n    thisjob = newjob;\n#endif\n"
    if needle not in exec_source:
        raise SystemExit("foreground state boundary not found")
    exec_source = exec_source.replace(needle, replacement, 1)
# This branch is only for stopped process-group pipeline jobs.  Astia has no
# process groups, so entering it forks a shell helper and skips the foreground
# wait even when the command is an ordinary simple command.
if "ASTIA no process-group pipeline branch" not in exec_source:
    needle = "!subsh && jobbing &&"
    replacement = "!subsh /* ASTIA no process-group pipeline branch */ && 0 &&"
    if needle not in exec_source:
        raise SystemExit("job-control pipeline boundary not found")
    exec_source = exec_source.replace(needle, replacement, 1)
# The entersubsh report only carries process-group metadata.  With Astia's
# process groups disabled, waiting for that auxiliary pipe can deadlock the
# parent before it reaches the ordinary foreground wait.
if "ASTIA skips process-group handshake" not in exec_source:
    needle = "\tclose(synch[1]);\n\tread_loop(synch[0], (char *)&esret, sizeof(esret));"
    replacement = "#ifdef ASTIA /* ASTIA skips process-group handshake */\n\tclose(synch[1]);\n#else\n" + needle + "\n#endif"
    if needle not in exec_source:
        raise SystemExit("execcmd_fork parent handshake boundary not found")
    exec_source = exec_source.replace(needle, replacement, 1)
    needle = "    if (write_loop(synch[1], (const void *) &esret, sizeof(esret)) != sizeof(esret)) {"
    replacement = "#ifndef ASTIA /* ASTIA skips process-group handshake */\n" + needle
    if needle not in exec_source:
        raise SystemExit("execcmd_fork child handshake boundary not found")
    exec_source = exec_source.replace(needle, replacement, 1)
    needle = "    close(synch[1]);\n    zclose(close_if_forked);"
    replacement = "#endif\n" + needle
    if needle not in exec_source:
        raise SystemExit("execcmd_fork child handshake close boundary not found")
    exec_source = exec_source.replace(needle, replacement, 1)
write(exec_path, exec_source)

# Astia does not provide process groups yet, so keep monitor mode off and make
# tty hand-off a no-op.  This does not affect the shell's ordinary foreground
# wait/reap path.
utils_source = read(utils_path)
attach_block = "#ifdef ASTIA /* ASTIA job control: no process groups yet */\n    opts[MONITOR] = 0;\n    return;\n#endif"
utils_source, attach_replaced = re.subn(
    r"#ifdef ASTIA /\* ASTIA job control: no process groups yet \*/.*?#endif",
    attach_block, utils_source, count=1, flags=re.S)
if not attach_replaced:
    needle = "attachtty(pid_t pgrp)\n{"
    replacement = needle + "\n" + attach_block + "\n"
    if needle not in utils_source:
        raise SystemExit("attachtty boundary not found")
    utils_source = utils_source.replace(needle, replacement, 1)
write(utils_path, utils_source)

init_source = read(init_path)
if "ASTIA disables monitor until process groups exist" not in init_source:
    needle = "    mypid = (zlong)getpid();"
    replacement = needle + "\n#ifdef ASTIA /* ASTIA disables monitor until process groups exist */\n    opts[MONITOR] = 0;\n#endif"
    if needle not in init_source:
        raise SystemExit("init monitor boundary not found")
    init_source = init_source.replace(needle, replacement, 1)
write(init_path, init_source)

# Keep zsh's normal SIGCHLD path.  Foreground child ownership remains in the
# synchronous waitjobs() port below; no polling workaround is needed.
jobs_source = read(jobs_path)
jobs_source = remove_block(jobs_source, "ASTIA waitjobs")
needle = "void\nwaitjobs(void)\n{"
block = """void
waitjobs(void)
{
#ifdef ASTIA /* ASTIA waitjobs: synchronous kernel reap */
    {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        Job jn;
        Process pn;
        if (pid >= 0 && findproc(pid, &jn, &pn, 0)) {
            update_process(pn, status);
            update_job(jn);
        }
    }
    thisjob = -1;
    return;
#endif
"""
if "ASTIA waitjobs: synchronous kernel reap" not in jobs_source:
    if needle not in jobs_source:
        raise SystemExit("waitjobs boundary not found")
    jobs_source = jobs_source.replace(needle, block, 1)
write(jobs_path, jobs_source)

signals_source = read(signals_path)
signals_source = remove_block(signals_source, "ASTIA signal polling")
# A previous patch revision removed sigsuspend() while deleting its polling
# workaround, leaving ret uninitialised.  Keep zsh's normal signal wait path.
if "ret = sigsuspend(&set);" not in signals_source:
    needle = "\n    return ret;"
    if needle not in signals_source:
        raise SystemExit("signal_suspend return boundary not found")
    signals_source = signals_source.replace(
        needle, "\n    ret = sigsuspend(&set);" + needle, 1)
# Foreground waitjobs() owns child reaping on Astia.  Letting the SIGCHLD
# handler reap first loses the PID before zsh can update its job table.
if "ASTIA SIGCHLD reaped by waitjobs" not in signals_source:
    needle = "    case SIGCHLD:\n\twait_for_processes();\n        break;"
    replacement = "    case SIGCHLD:\n#ifdef ASTIA /* ASTIA SIGCHLD reaped by waitjobs */\n        break;\n#else\n\twait_for_processes();\n        break;\n#endif"
    if needle not in signals_source:
        raise SystemExit("SIGCHLD handler boundary not found")
    signals_source = signals_source.replace(needle, replacement, 1)
write(signals_path, signals_source)
PY
