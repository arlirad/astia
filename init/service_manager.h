#pragma once

/* A deliberately small native init policy: /etc/rc contains one executable
 * per line, with blank lines and '#' comments ignored.  It is not a shell
 * script; arguments, expansion, redirection, and pipelines are unsupported. */
int astia_service_manager(const char* rc_path);
