/* NexOS — kernel/proc/elf.h | ELF Binary Loader | MIT License */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

typedef struct {
    uint8_t  e_ident[16];  // magic: 0x7F,'E','L','F'
    uint16_t e_type;       // 2=executable
    uint16_t e_machine;    // 0x3E = x86_64
    uint32_t e_version;
    uint64_t e_entry;      // entry point address
    uint64_t e_phoff;      // program header offset
    uint64_t e_shoff;      // section header offset
    uint32_t e_flags;
    uint16_t e_ehsize;     // ELF header size (64)
    uint16_t e_phentsize;  // program header entry size
    uint16_t e_phnum;      // number of program headers
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_hdr_t;

typedef struct {
    uint32_t p_type;    // 1=PT_LOAD
    uint32_t p_flags;   // PF_R=4, PF_W=2, PF_X=1
    uint64_t p_offset;  // offset in file
    uint64_t p_vaddr;   // virtual address to load at
    uint64_t p_paddr;   // physical address
    uint64_t p_filesz;  // size in file
    uint64_t p_memsz;   // size in memory (>filesz means BSS)
    uint64_t p_align;   // alignment
} __attribute__((packed)) elf64_phdr_t;

int elf_load(const char *path, uint64_t *entry_out);

#endif