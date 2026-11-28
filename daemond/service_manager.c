#include "service_manager.h"

#include "daemond.h"

/* Source compatibility for callers that used the compatibility entry point. */
int astia_service_manager(const char* unit_directory) {
    return astia_daemon_run(unit_directory);
}
