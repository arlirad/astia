/* Dependency-closure fixture: imports dl_dep_value from libdlfixture_dep.so. */
extern int dl_dep_value(void);

int dl_user_value(void) {
    return dl_dep_value();
}
