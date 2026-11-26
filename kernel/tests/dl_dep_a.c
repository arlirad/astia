/* Diamond root: needs both branches, which need the same base. */
extern int dl_b_value(void);
extern int dl_c_value(void);

int dl_a_value(void) {
    return dl_b_value() + dl_c_value();
}
