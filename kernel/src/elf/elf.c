#include "elf.h"
#include <stdint.h>
#include <lib/string.h>

#define ELF_PT_LOAD 1u
#define ELF_MAX_PHNUM 128u
#define NTUX_USER_VADDR_MAX 0x80000000ull

static inline int is_valid_elf(const Elf64_Ehdr *ehdr)
{
    return ehdr &&
           ehdr->e_ident[0] == 0x7f &&
           ehdr->e_ident[1] == 'E'  &&
           ehdr->e_ident[2] == 'L'  &&
           ehdr->e_ident[3] == 'F'  &&
           ehdr->e_ident[4] == 2     &&
           ehdr->e_ident[5] == 1     &&
           ehdr->e_version  == 1     &&
           ehdr->e_machine  == 0x3e  &&
           (ehdr->e_type == 2 || ehdr->e_type == 3);
}

static int range_add_overflow_u64(uint64_t base, uint64_t len, uint64_t* end_out) {
    if (!end_out) return -1;
    if (len > UINT64_MAX - base) return -1;
    *end_out = base + len;
    return 0;
}

static int validate_phdr_table(const Elf64_Ehdr* ehdr, size_t elf_size) {
    if (!ehdr) return -1;
    if (ehdr->e_ehsize < sizeof(Elf64_Ehdr)) return -1;
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr)) return -1;
    if (ehdr->e_phnum == 0 || ehdr->e_phnum > ELF_MAX_PHNUM) return -1;
    if (ehdr->e_phoff > elf_size) return -1;

    uint64_t table_size = (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    uint64_t table_end = 0;
    if (range_add_overflow_u64(ehdr->e_phoff, table_size, &table_end) != 0) return -1;
    if (table_end > elf_size) return -1;
    return 0;
}

static int copy_load_segment(const Elf64_Phdr *ph, const void *elf_base, size_t elf_size) {
    if (!ph || !elf_base) return -1;
    if (ph->p_type != ELF_PT_LOAD) return 0;
    if (ph->p_filesz > ph->p_memsz) return -1;

    uint64_t src_end = 0;
    if (range_add_overflow_u64(ph->p_offset, ph->p_filesz, &src_end) != 0) return -1;
    if (src_end > elf_size) return -1;

    if (ph->p_memsz == 0) return 0;

    uint64_t seg_end = 0;
    if (range_add_overflow_u64(ph->p_vaddr, ph->p_memsz, &seg_end) != 0) return -1;
    if (ph->p_vaddr >= NTUX_USER_VADDR_MAX || seg_end > NTUX_USER_VADDR_MAX) return -1;

    void* dest = (void*)(uintptr_t)ph->p_vaddr;
    memcpy(dest, (const char*)elf_base + ph->p_offset, (size_t)ph->p_filesz);
    if (ph->p_memsz > ph->p_filesz) {
        memset((char*)dest + ph->p_filesz, 0, (size_t)(ph->p_memsz - ph->p_filesz));
    }
    return 1;
}

int load_elf_image(const void *elf_base, size_t elf_size, uint64_t *entry_out)
{
    if (!elf_base || !entry_out || elf_size < sizeof(Elf64_Ehdr)) {
        return -1;
    }

    const Elf64_Ehdr *ehdr = elf_base;
    if (!is_valid_elf(ehdr)) {
        return -1;
    }
    if (validate_phdr_table(ehdr, elf_size) != 0) return -1;

    const Elf64_Phdr *ph = (const Elf64_Phdr *)((uintptr_t)elf_base + ehdr->e_phoff);
    uint32_t loadable_count = 0;
    int entry_in_load_segment = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++, ph++) {
        int rc = copy_load_segment(ph, elf_base, elf_size);
        if (rc < 0) return -1;
        if (rc > 0) {
            loadable_count++;
            if (ehdr->e_entry >= ph->p_vaddr && ehdr->e_entry < ph->p_vaddr + ph->p_memsz) {
                entry_in_load_segment = 1;
            }
        }
    }

    if (loadable_count == 0 || !entry_in_load_segment) return -1;
    *entry_out = ehdr->e_entry;
    return 0;
}

int run_elf_module_with_api(const void *elf_base, size_t elf_size, const void *api_ptr, const char **ret_string)
{
    uint64_t entry = 0;
    if (load_elf_image(elf_base, elf_size, &entry) != 0) {
        return -1;
    }

    typedef const char *(*module_entry_t)(const void *);
    module_entry_t fn = (module_entry_t)(uintptr_t)entry;
    const char *ret = fn(api_ptr);

    if (ret_string) {
        *ret_string = ret;
    }
    return 0;
}

int load_and_start_elf(const void *elf_base, size_t elf_size, uint64_t stack_top)
{
    uint64_t entry = 0;
    if (load_elf_image(elf_base, elf_size, &entry) != 0) {
        return -1;
    }

    uint64_t rsp = stack_top & ~0xF;

    asm volatile (
        "mov %0, %%rsp\n"
        "xor %%rbp, %%rbp\n"
        "xor %%rbx, %%rbx\n"
        "xor %%r12,%%r12\n"
        "xor %%r13,%%r13\n"
        "xor %%r14,%%r14\n"
        "xor %%r15,%%r15\n"
        "jmp *%1\n"
        :
        : "r"(rsp), "r"(entry)
        : "memory", "rbx", "r12", "r13", "r14", "r15"
    );

    return -2;
}
