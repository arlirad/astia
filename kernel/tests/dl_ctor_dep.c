/* Runtime dependency of libdlctor.so: loaded only from that library's
 * constructor and destructor, never linked into it. */
int dl_ctor_dep_value(void) {
    return 55;
}

/* The initrd reports inode 0, so dlmap keys an image by (mount, inode, size):
 * keep this image's size distinct from the other fixtures'. */
static const char dl_ctor_dep_identity_pad[16] __attribute__((used)) = "dlctor-dep-pad!";
