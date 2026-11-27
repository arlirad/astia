int ver_value_v1(void) {
    return 101;
}

int ver_value_v2(void) {
    return 202;
}

__asm__(".symver ver_value_v1,ver_value@VERS_1");
__asm__(".symver ver_value_v2,ver_value@@VERS_2");
