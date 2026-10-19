#!/bin/sh
set -eu

source=$1
test -d "$source/Src"

# The process-group operations are native now.  Only the auxiliary
# entersubsh metadata pipe is bypassed: Astia does not yet wake a parked pipe
# read when the peer writes, and the metadata is not needed for a simple job.
python3 - "$source/Src/exec.c" <<'PY'
import sys
path = sys.argv[1]
s = open(path).read()
needle = "\tclose(synch[1]);\n\tread_loop(synch[0], (char *)&esret, sizeof(esret));"
if "ASTIA skips process-group handshake" not in s:
    if needle not in s:
        raise SystemExit("execcmd_fork handshake boundary not found")
    s = s.replace(needle,
        "#ifdef ASTIA /* ASTIA skips process-group handshake */\n"
        "\tclose(synch[1]);\n#else\n" + needle + "\n#endif", 1)
    needle = "    if (write_loop(synch[1], (const void *) &esret, sizeof(esret)) != sizeof(esret)) {"
    if needle not in s:
        raise SystemExit("execcmd_fork child handshake boundary not found")
    s = s.replace(needle, "#ifndef ASTIA /* ASTIA skips process-group handshake */\n" + needle, 1)
    needle = "    close(synch[1]);\n    zclose(close_if_forked);"
    if needle not in s:
        raise SystemExit("execcmd_fork child close boundary not found")
    s = s.replace(needle, "#endif\n" + needle, 1)
    open(path, "w").write(s)
PY
