#include "service_manager.h"

#include "daemond.h"

/* Source compatibility for callers that used the compatibility entry point. */
int astia_service_manager(const char* rc_path) {
    return astia_daemon_run(rc_path);
}
