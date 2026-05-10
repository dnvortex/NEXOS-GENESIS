/* NexOS — kernel/proc/elf.c | ELF Binary Loader | MIT License */
#include "elf.h"
#include "../kernel.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include <stdint.h>
#include <stddef.h>

#define PT_LOAD 1
#define PF_R    4
#define PF_W    2
#define PF_X    1

static int memzero(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
    return 0;
}

int elf_load(const char *path, uint64_t *entry_out) {
    // a) Open file via vfs_open(path)
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) return -1;

    // b) Read ELF header (first 64 bytes)
    elf64_hdr_t hdr;
    uint32_t n = vfs_read(node, 0, sizeof(elf64_hdr_t), (uint8_t *)&hdr);
    if (n != sizeof(elf64_hdr_t)) {
        vfs_close(node);
        return -1;
    }

    // c) Verify magic: e_ident[0-3] = {0x7F,'E','L','F'}
    if (hdr.e_ident[0] != 0x7F || hdr.e_ident[1] != 'E' ||
        hdr.e_ident[2] != 'L' || hdr.e_ident[3] != 'F') {
        vfs_close(node);
        return -1;
    }

    // d) Verify e_machine = 0x3E (x86_64)
    if (hdr.e_machine != 0x3E) {
        vfs_close(node);
        return -1;
    }

    // e) Verify e_type = 2 (executable)
    if (hdr.e_type != 2) {
        vfs_close(node);
        return -1;
    }

    // f) Read all program headers (e_phnum of them)
    uint32_t phdr_size = hdr.e_phnum * hdr.e_phentsize;
    elf64_phdr_t *phdrs = (elf64_phdr_t *)kmalloc(phdr_size);
    if (!phdrs) {
        vfs_close(node);
        return -1;
    }
    n = vfs_read(node, hdr.e_phoff, phdr_size, (uint8_t *)phdrs);
    if (n != phdr_size) {
        kfree(phdrs);
        vfs_close(node);
        return -1;
    }

    // g) For each PT_LOAD segment:
    for (uint16_t i = 0; i < hdr.e_phnum; i++) {
        elf64_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        // Allocate pages for [p_vaddr, p_vaddr+p_memsz]
        uint64_t start = ph->p_vaddr;
        uint64_t end = ph->p_vaddr + ph->p_memsz;
        uint64_t flags = VMM_FLAG_USER;
        if (ph->p_flags & PF_W) flags |= VMM_FLAG_WRITE;
        // Note: VMM_FLAG_NX not set for executable, but since it's user, maybe need to handle NX

        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                // TODO: free allocated pages
                kfree(phdrs);
                vfs_close(node);
                return -1;
            }
            vmm_map(addr, phys, flags);
        }

        // Read p_filesz bytes from file at p_offset into memory at p_vaddr
        n = vfs_read(node, ph->p_offset, ph->p_filesz, (uint8_t *)ph->p_vaddr);
        if (n != ph->p_filesz) {
            // TODO: free
            kfree(phdrs);
            vfs_close(node);
            return -1;
        }

        // Zero remaining p_memsz - p_filesz bytes (BSS)
        if (ph->p_memsz > ph->p_filesz) {
            memzero((void *)(ph->p_vaddr + ph->p_filesz), ph->p_memsz - ph->p_filesz);
        }
    }

    // h) Set *entry_out = e_entry
    *entry_out = hdr.e_entry;

    // i) Return 0 on success
    kfree(phdrs);
    vfs_close(node);
    return 0;
}