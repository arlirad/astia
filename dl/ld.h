#pragma once

#include <stddef.h>
#include <stdint.h>

/* The ELF64 subset the linker needs.  Deliberately not <elf.h>: this runs
 * freestanding, and the layout is the ABI, not the host's headers. */
typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} Elf64_Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

typedef struct {
    uint16_t vn_version;
    uint16_t vn_cnt;
    uint32_t vn_file;
    uint32_t vn_aux;
    uint32_t vn_next;
} Elf64_Verneed;

typedef struct {
    uint32_t vna_hash;
    uint16_t vna_flags;
    uint16_t vna_other;
    uint32_t vna_name;
    uint32_t vna_next;
} Elf64_Vernaux;

typedef struct {
    uint16_t vd_version;
    uint16_t vd_flags;
    uint16_t vd_ndx;
    uint16_t vd_cnt;
    uint32_t vd_hash;
    uint32_t vd_aux;
    uint32_t vd_next;
} Elf64_Verdef;

typedef struct {
    uint32_t vda_name;
    uint32_t vda_next;
} Elf64_Verdaux;

enum {
    PT_LOAD      = 1,
    PT_DYNAMIC   = 2,
    PT_PHDR      = 6,
    PT_TLS       = 7,
    PT_GNU_RELRO = 0x6474e552,
};

enum {
    DT_NULL         = 0,
    DT_NEEDED       = 1,
    DT_PLTRELSZ     = 2,
    DT_HASH         = 4,
    DT_STRTAB       = 5,
    DT_SYMTAB       = 6,
    DT_RELA         = 7,
    DT_RELASZ       = 8,
    DT_RELAENT      = 9,
    DT_SYMENT       = 11,
    DT_SONAME       = 14,
    DT_PLTREL       = 20,
    DT_GNU_HASH     = 0x6ffffef5,
    DT_INIT         = 12,
    DT_FINI         = 13,
    DT_INIT_ARRAY   = 25,
    DT_FINI_ARRAY   = 26,
    DT_INIT_ARRAYSZ = 27,
    DT_FINI_ARRAYSZ = 28,
    DT_JMPREL       = 23,
    DT_VERSYM       = 0x6ffffff0,
    DT_VERDEF       = 0x6ffffffc,
    DT_VERDEFNUM    = 0x6ffffffd,
    DT_VERNEED      = 0x6ffffffe,
    DT_VERNEEDNUM   = 0x6fffffff,
    DT_PLTGOT       = 3,
    DT_BIND_NOW     = 24,
    DT_FLAGS        = 30,
    DT_FLAGS_1      = 0x6ffffffb,
};

enum {
    DF_BIND_NOW = 0x8,
    DF_1_NOW    = 0x1,
};

/* Symbol type from st_info & 0xf. */
enum { LD_STT_GNU_IFUNC = 10 };

/* Auxiliary vector tags. */
enum {
    AT_NULL   = 0,
    AT_PHDR   = 3,
    AT_PHENT  = 4,
    AT_PHNUM  = 5,
    AT_BASE   = 7,
    AT_ENTRY  = 9,
    AT_EXECFN = 31,
    /* astia extensions: the static TLS arena the kernel reserved. */
    AT_ASTIA_TLS_BASE     = 0x41,
    AT_ASTIA_TLS_TP       = 0x42,
    AT_ASTIA_TLS_LIMIT    = 0x43,
    AT_ASTIA_PAYLOAD_BASE = 0x44,
};

/* Symbol types: only the one the linker distinguishes matters. */
#define LD_STT_GNU_IFUNC 10

#define LD_MAX_MODULES 32
#define LD_MAX_HANDLES 16
#define LD_MAX_IFUNCS 64
#define LD_PATH_MAX 64

typedef struct {
    uint64_t base;
    /* Absolute executable PT_LOAD bounds; vaddr_end is bias-relative. */
    uint64_t             text_start;
    uint64_t             text_end;
    uint64_t             vaddr_end;
    const Elf64_Phdr*    phdr;
    uint32_t             phnum;
    uint64_t             span;
    const Elf64_Dyn*     dynamic_section;
    const char*          strtab;
    const Elf64_Sym*     symtab;
    const uint32_t*      hash;     /* DT_HASH, when present */
    const uint32_t*      gnu_hash; /* DT_GNU_HASH, when present */
    const char*          soname;
    const uint16_t*      versym;  /* DT_VERSYM, when present */
    const Elf64_Verneed* verneed; /* DT_VERNEED, when present */
    uint32_t             verneednum;
    const Elf64_Verdef*  verdef; /* DT_VERDEF, when present */
    uint32_t             verdefnum;
    const Elf64_Rela*    rela;
    uint64_t             rela_size;
    const Elf64_Rela*    jmprel;
    uint64_t             pltrel_size;
    uint64_t             pltrel_count;
    uint64_t             pltrel;
    uint64_t*            pltgot;
    uint64_t             pending_plt;
    int                  lazy;
    uint64_t             init;
    const uint64_t*      init_array;
    uint64_t             init_array_size;
    uint64_t             fini;
    const uint64_t*      fini_array;
    uint64_t             fini_array_size;
    uint32_t             symbol_count;
    uint32_t             id;
    /* PT_TLS template and the static-TLS offset assigned to this module. */
    uint64_t tls_vaddr;
    uint64_t tls_filesz;
    uint64_t tls_memsz;
    uint64_t tls_align;
    int64_t  tls_offset;
    /* Absolute PT_GNU_RELRO start (base + p_vaddr) and its p_memsz. */
    uint64_t relro_start;
    uint64_t relro_size;
    /* Registry and handle state. */
    int      used;
    int      startup;
    int      dynamic;
    int      prelinked;
    int      tls_static;
    int      nodelete;
    int      in_global;
    int      pending;
    int      finalized;
    uint32_t refcount;
    char     name[LD_PATH_MAX];
} ld_module;
