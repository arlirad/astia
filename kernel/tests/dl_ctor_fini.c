/* Destructor evidence library: libdlctor.so opens it with RTLD_NODELETE from
 * its destructor, so the payload can still see it after dlclose returns and
 * thereby prove the destructor's loader calls ran. */
int dl_ctor_fini_value(void) {
    return 56;
}

/* The initrd reports inode 0, so dlmap keys an image by (mount, inode, size):
 * keep this image's size distinct from the other fixtures'. */
static const char dl_ctor_fini_identity_pad[32] __attribute__((used)) =
    "dlctor-fini-identity-padding!!!!";
