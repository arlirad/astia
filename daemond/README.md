# daemond units

`daemond` reads one unit per `*.yaml` file from `/etc/daemond/`. The filename
without `.yaml` is the unit name; there is no `name:` key. A missing directory
or a directory with no units leaves the supervisor resident and idle so PID 1
stays alive. The old `/etc/rc` line format is no longer read.

```yaml
description: OpenSSH daemon
exec: /usr/sbin/sshd
args:
  - -D
  - -e
restart: on-failure        # no | always | on-failure
restart-limit: 3           # 0..16, defaults to 3 when restart is enabled
working-directory: /
environment:
  PATH: /bin:/usr/bin
```

The parser accepts a controlled YAML subset: comments, `key: value` maps,
block lists and maps introduced by `args:` and `environment:`, and single- or
double-quoted scalars. Tabs, anchors, flow collections, multi-document files
and unknown keys are rejected. A rejected unit stops startup with the file and
line in the log; it is never silently ignored.

`daemonctl status|start|stop|restart|validate|list` operates on the supervisor
through `/run/daemon.sock`; `list` and `status` report unit names. The host
tests under `tests/` cover the parser, restart policy, stop escalation and
control-socket framing.
