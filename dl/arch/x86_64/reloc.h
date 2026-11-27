#pragma once

/* x86-64 dynamic relocation types used by the interpreter. */
enum {
    LD_R_NONE      = 0,
    LD_R_ABS       = 1, /* R_X86_64_64 */
    LD_R_COPY      = 5, /* R_X86_64_COPY */
    LD_R_GLOB_DAT  = 6,
    LD_R_JUMP_SLOT = 7,
    LD_R_RELATIVE  = 8,
    LD_R_DTPMOD64  = 16,
    LD_R_DTPOFF64  = 17,
    LD_R_TPOFF64   = 18,
    LD_R_TPOFF32   = 23,
    LD_R_IRELATIVE = 37,
};
