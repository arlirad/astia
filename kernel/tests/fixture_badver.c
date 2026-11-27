/* Library for the version rejection test.  The staged variant is built with
 * -DONLY_V2 so it exports only the default definition; the link-time variant
 * carries both VERS_1 and VERS_2. */
#ifndef ONLY_V2
int bad_value_v1(void) {
    return 11;
}

__asm__(".symver bad_value_v1,bad_value@VERS_1");
#endif

int bad_value_v2(void) {
    return 22;
}

__asm__(".symver bad_value_v2,bad_value@@VERS_2");
