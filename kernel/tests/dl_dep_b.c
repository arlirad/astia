/* Diamond left branch.  The padding keeps this image's size distinct from
 * libdl_dep_c.so: the initrd filesystem reports inode 0, so dlmap's identity
 * key is (mount, inode, size) and equal-sized files would collide. */
extern int dl_base_value(void);

static const char dl_b_identity_pad[8] __attribute__((used)) = "b";

int dl_b_value(void) {
    return dl_base_value() + 1;
}
