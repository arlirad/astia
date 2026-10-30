#include "service_manager.h"

#include "daemon.h"

/* Source compatibility for callers that used the old in-process manager. */
int astia_service_manager(const char* rc_path) {
    return astia_daemon_run(rc_path);
}
