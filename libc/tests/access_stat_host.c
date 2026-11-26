#include <astia/api/aio.h>

#include <stdint.h>

extern long __astia_host_fio_statat(int dirfd, const char* path, void* out,
                                    uint32_t length, int flags);

static int      override_active;
static uint32_t override_uid;
static uint32_t override_gid;
static uint32_t override_mode;

void __astia_test_stat_override(unsigned int uid, unsigned int gid, unsigned int mode) {
    override_uid    = uid;
    override_gid    = gid;
    override_mode   = mode;
    override_active = 1;
}

void __astia_test_stat_clear(void) {
    override_active = 0;
}

long __astia_fio_statat(int dirfd, const char* path, void* out, uint32_t length,
                        int flags) {
    long rc = __astia_host_fio_statat(dirfd, path, out, length, flags);

    if (rc < 0 || !override_active)
        return rc;

    astia_stat* value = (astia_stat*) out;

    value->uid  = override_uid;
    value->gid  = override_gid;
    value->mode = (value->mode & ~0777u) | (override_mode & 0777u);

    return rc;
}
