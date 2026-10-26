#!/usr/bin/env python3
import sys

path = sys.argv[1]
text = open(path).read()
if "ASTIA blocking child wait" in text:
    raise SystemExit(0)

marker = "/**/\nint\nsignal_suspend"
if marker not in text:
    raise SystemExit("zsh signal state marker not found")
text = text.replace(
    marker,
    "int astia_wait_blocking;\n" + marker,
    1,
)

old = """#ifdef HAVE_WAIT3
# ifdef HAVE_GETRUSAGE
\tstruct rusage ru;

\tpid = wait3((void *)&status, WAITFLAGS, &ru);
# else
\tpid = wait3((void *)&status, WAITFLAGS, NULL);
# endif
#else
# ifdef HAVE_WAITPID
\tpid = waitpid(-1, &status, WAITFLAGS);
# else
\tpid = wait(&status);
# endif
#endif"""
new = """#ifdef ASTIA
\t/* Astia delivers child completion through native waitpid.  Use one
\t * blocking wait when signal_suspend asked us to sleep; subsequent
\t * iterations drain already-completed children without polling. */
\tpid = waitpid(-1, &status,
\t              astia_wait_blocking ? (WAITFLAGS & ~WNOHANG) : WAITFLAGS);
\tastia_wait_blocking = 0;
#else
#ifdef HAVE_WAIT3
# ifdef HAVE_GETRUSAGE
\tstruct rusage ru;

\tpid = wait3((void *)&status, WAITFLAGS, &ru);
# else
\tpid = wait3((void *)&status, WAITFLAGS, NULL);
# endif
#else
# ifdef HAVE_WAITPID
\tpid = waitpid(-1, &status, WAITFLAGS);
# else
\tpid = wait(&status);
# endif
#endif
#endif"""
if old not in text:
    raise SystemExit("zsh wait/reap block not found")
text = text.replace(old, new, 1)

old = """#ifdef ASTIA /* ASTIA signal polling */
    wait_for_processes();
    sched_yield();
    ret = -1;
#else
    ret = sigsuspend(&set);
#endif"""
new = """#ifdef ASTIA /* ASTIA blocking child wait */
    astia_wait_blocking = 1;
    wait_for_processes();
    ret = -1;
#else
    ret = sigsuspend(&set);
#endif"""
if old not in text:
    raise SystemExit("zsh signal suspend block not found")
text = text.replace(old, new, 1)

old = """    case SIGCHLD:
\twait_for_processes();
        break;"""
new = """    case SIGCHLD:
#ifndef ASTIA
\twait_for_processes();
#endif
        break;"""
if old not in text:
    raise SystemExit("zsh SIGCHLD handler block not found")
text = text.replace(old, new, 1)

open(path, "w").write(text)
