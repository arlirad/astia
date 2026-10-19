/* zsh's rlimits module has no Astia backend yet.  Keep its module seam
 * linkable; the shell simply has no adjustable resource limits in v1. */
void set_resinfo(void) {}
void free_resinfo(void) {}
