/* Diamond right branch.  The bigger padding keeps this image's size distinct
 * from libdl_dep_b.so because initrd files report inode 0. */
extern int dl_base_value(void);

static const char dl_c_identity_pad[4096] __attribute__((used)) = {1};

int dl_c_value(void) {
    return dl_base_value() + 2;
}
